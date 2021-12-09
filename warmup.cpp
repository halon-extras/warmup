#include <HalonMTA.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <thread>
#include <syslog.h>
#include <list>
#include <set>
#include <map>
#include <mutex>
#include <cstring>

struct rate_t
{
	size_t messages;
	double interval;
};
struct ip_t
{
	std::string ip;
	std::string helo;
	std::string class_;
	time_t added;
};
struct schedule_t
{
	int fields = HALONMTA_QUEUE_LOCALIP;
	std::map<int, std::string> if_;
	std::map<size_t, rate_t> days;
};
struct added_t
{
	char* id;
	int fields = HALONMTA_QUEUE_LOCALIP;
	std::map<int, std::string> if_;
	size_t messages;
	double interval;
};

std::map<std::string, schedule_t> schedules;
std::list<ip_t> ips;
std::mutex lock;
std::map<std::pair<std::string, std::string>, added_t> policies;

bool stop = false;
std::thread p;

bool parseConfigSchedule(HalonConfig* cfg, std::map<std::string, schedule_t>& schedules);
bool parseConfigIPs(HalonConfig*, const std::map<std::string, schedule_t>& schedules, std::list<ip_t>&);
void update_rates();

HALON_EXPORT
bool Halon_early_init(HalonInitContext* hic)
{
	HalonConfig *cfg, *app;
	HalonMTA_init_getinfo(hic, HALONMTA_INIT_CONFIG, nullptr, 0, &cfg, nullptr);
	HalonMTA_init_getinfo(hic, HALONMTA_INIT_APPCONFIG, nullptr, 0, &app, nullptr);

	if (!parseConfigSchedule(app, schedules))
		if (!parseConfigSchedule(cfg, schedules))
			return false;

	if (schedules.empty())
	{
		syslog(LOG_CRIT, "No schedules");
		return false;
	}

	if (!parseConfigIPs(app, schedules, ips))
		return false;

	for (const auto & ip : ips)
		HalonMTA_queue_list_add(HALONMTA_QUEUE_LOCALIP, ip.class_.c_str());
	for (const auto & ip : ips)
		HalonMTA_queue_list_item_add(HALONMTA_QUEUE_LOCALIP, ip.class_.c_str(), ip.ip.c_str());
	HalonMTA_queue_list_reload(0, nullptr);

	return true;
}

bool Halon_init(HalonInitContext* hic)
{
	update_rates();

	p = std::thread([] {
		while (true)
		{
			for (size_t i = 0; i < 5 && !stop; ++i)
				sleep(1);
			if (stop)
				break;
			update_rates();
		}
	});

	return true;
}

HALON_EXPORT
void Halon_config_reload(HalonConfig* hc)
{
	{
		std::lock_guard<std::mutex> lg(lock);

		syslog(LOG_INFO, "WarmUP: reloading");

		std::map<std::string, schedule_t> schedules2;
		if (parseConfigSchedule(hc, schedules2))
			schedules = schedules2;

		std::list<ip_t> ips_new;
		if (parseConfigIPs(hc, schedules, ips_new))
		{
			std::set<std::string> class_;
			for (const auto & o : ips)
			{
				bool f = false;
				for (const auto & n : ips_new)
				{
					if (o.ip == n.ip && o.class_ == n.class_)
					{
						f = true;
						break;
					}
				}
				if (!f)
				{
					HalonMTA_queue_list_item_delete(HALONMTA_QUEUE_LOCALIP, o.class_.c_str(), o.ip.c_str());
					class_.insert(o.class_);
				}
			}
			for (const auto & n : ips_new)
			{
				bool f = false;
				for (const auto & o : ips)
				{
					if (o.ip == n.ip && o.class_ == n.class_)
					{
						f = true;
						break;
					}
				}
				if (!f)
				{
					HalonMTA_queue_list_add(HALONMTA_QUEUE_LOCALIP, n.class_.c_str());
					HalonMTA_queue_list_item_add(HALONMTA_QUEUE_LOCALIP, n.class_.c_str(), n.ip.c_str());
					class_.insert(n.class_);
				}
			}
			for (const auto & c : class_)
				HalonMTA_queue_list_reload(HALONMTA_QUEUE_LOCALIP, c.c_str());
			ips = ips_new;
		}

		for (auto it = policies.begin(); it != policies.end();)
		{
			bool found = false;
			for (const auto & ip : ips)
			{
				if (ip.class_ == it->first.first && ip.ip == it->first.second)
				{
					found = true;
					break;
				}
			}
			if (found)
				++it;
			else
			{
				if (it->second.id)
				{
					HalonMTA_queue_policy_delete(it->second.id);
					free(it->second.id);
					syslog(LOG_INFO, "WarmUP: untrack ip:%s class:%s", it->first.second.c_str(), it->first.first.c_str());
				}
				policies.erase(it++);
			}
		}
	}

	update_rates();
}

/* function to get all ips from database in order of warmup days, and random... */
/*
 * Try([
 *		"sourceip" => warmup_ips(),
 *		"sourceip_random" => false,
 *	]);
 */
HALON_EXPORT
void warmup_ips(HalonHSLContext* hhc, HalonHSLArguments* args, HalonHSLValue* ret)
{
	HalonHSLValue* x = HalonMTA_hsl_argument_get(args, 0);
	char* class_ = nullptr;
	if (x && HalonMTA_hsl_value_type(x) == HALONMTA_HSL_TYPE_STRING)
		HalonMTA_hsl_value_get(x, HALONMTA_HSL_TYPE_STRING, &class_, nullptr);

	double i = 0;
	HalonMTA_hsl_value_set(ret, HALONMTA_HSL_TYPE_ARRAY, nullptr, 0);

	std::lock_guard<std::mutex> lg(lock);
	for (auto & ip : ips)
	{
		if (class_ && ip.class_ != class_)
			continue;
		HalonHSLValue *k, *v;
		HalonMTA_hsl_value_array_add(ret, &k, &v);
		HalonMTA_hsl_value_set(k, HALONMTA_HSL_TYPE_NUMBER, (void*)&i, 0);
		HalonHSLValue *k2, *v2;
		HalonMTA_hsl_value_array_add(v, &k2, &v2);
		HalonMTA_hsl_value_set(k2, HALONMTA_HSL_TYPE_STRING, "address", 0);
		HalonMTA_hsl_value_set(v2, HALONMTA_HSL_TYPE_STRING, (const char*)ip.ip.c_str(), 0);
		if (!ip.helo.empty())
		{
			HalonMTA_hsl_value_array_add(v, &k2, &v2);
			HalonMTA_hsl_value_set(k2, HALONMTA_HSL_TYPE_STRING, "helo", 0);
			HalonMTA_hsl_value_set(v2, HALONMTA_HSL_TYPE_STRING, (const char*)ip.helo.c_str(), 0);
		}
		++i;
	}
	return;
}

HALON_EXPORT
void Halon_cleanup()
{
	stop = true;
	p.join();
}

HALON_EXPORT
bool Halon_hsl_register(HalonHSLRegisterContext* ptr)
{
	HalonMTA_hsl_register_function(ptr, "warmup_ips", &warmup_ips);
	return true;
}

HALON_EXPORT
int Halon_version()
{
	return HALONMTA_PLUGIN_VERSION;
}

void update_rates()
{
	std::lock_guard<std::mutex> lg(lock);

	time_t now = time(nullptr);
	for (const auto & ip_ : ips)
	{
		ssize_t days_ = now < ip_.added ? 0 : (now - ip_.added) / (3600 * 24);

		size_t messages = 0;
		double interval = 0;
		size_t days = 0;

		auto schedule = schedules.find(ip_.class_);
		for (auto s = schedule->second.days.rbegin(); s != schedule->second.days.rend(); s++)
		{
			if (days_ > s->first)
				break;
			days = s->first;
			messages = s->second.messages;
			interval = s->second.interval;
		}

		auto it = policies.find({ ip_.class_, ip_.ip });
		if (it == policies.end())
		{
			if (messages > 0)
			{
readd:
				std::map<int, std::string>::iterator ifi;
				const char* transportid =
					(ifi = schedule->second.if_.find(HALONMTA_QUEUE_TRANSPORTID)) != schedule->second.if_.end()
					? ifi->second.c_str()
					: nullptr;
				const char* remoteip =
					(ifi = schedule->second.if_.find(HALONMTA_QUEUE_REMOTEIP)) != schedule->second.if_.end()
					? ifi->second.c_str()
					: nullptr;
				const char* remotemx =
					(ifi = schedule->second.if_.find(HALONMTA_QUEUE_REMOTEMX)) != schedule->second.if_.end()
					? ifi->second.c_str()
					: nullptr;
				const char* recipientdomain =
					(ifi = schedule->second.if_.find(HALONMTA_QUEUE_RECIPIENTDOMAIN)) != schedule->second.if_.end()
					? ifi->second.c_str()
					: nullptr;
				const char* jobid =
					(ifi = schedule->second.if_.find(HALONMTA_QUEUE_JOBID)) != schedule->second.if_.end()
					? ifi->second.c_str()
					: nullptr;

				auto p = HalonMTA_queue_policy_add(schedule->second.fields, transportid, ip_.ip.c_str(), remoteip, remotemx, recipientdomain, jobid, 0, messages, interval, std::string(std::string("Day_") + std::to_string(days)).c_str(), 0);
				if (!p)
					syslog(LOG_CRIT, "WarmUP: failed to add policy for ip:%s class:%s days:%ld rate:%zu/%f", ip_.ip.c_str(), ip_.class_.c_str(), days, messages, interval);
				else
					syslog(LOG_INFO, "WarmUP: ip:%s class:%s days:%ld rate:%zu/%f", ip_.ip.c_str(), ip_.class_.c_str(), days, messages, interval);
				policies[{ ip_.class_, ip_.ip }] = { p, schedule->second.fields, schedule->second.if_, messages, interval };
			}
		}
		else
		{
			if (messages <= 0)
			{
				HalonMTA_queue_policy_delete(it->second.id);
				free(it->second.id);
				syslog(LOG_INFO, "WarmUP: ip:%s class:%s days:%ld (no limit)", ip_.ip.c_str(), ip_.class_.c_str(), days_);
				policies.erase(it);
			}
			else if (it->second.fields != schedule->second.fields || it->second.if_ != schedule->second.if_)
			{
				HalonMTA_queue_policy_delete(it->second.id);
				free(it->second.id);
				syslog(LOG_INFO, "WarmUP: ip:%s class:%s days:%ld (update)", ip_.ip.c_str(), ip_.class_.c_str(), days_);
				policies.erase(it);
				goto readd;
			}
			else if (it->second.messages != messages || it->second.interval != interval)
			{
				HalonMTA_queue_policy_update(it->second.id, 0, messages, interval, std::string(std::string("Day_") + std::to_string(days)).c_str(), 0);
				syslog(LOG_INFO, "WarmUP: ip:%s class:%s days:%ld rate:%zu/%f->%zu/%f", ip_.ip.c_str(), ip_.class_.c_str(), days, it->second.messages, it->second.interval, messages, interval);
				it->second.messages = messages;
				it->second.interval = interval;
			}
		}
	}
}

bool parseConfigSchedule(HalonConfig* cfg, std::map<std::string, schedule_t>& schedules)
{
	auto s = HalonMTA_config_object_get(cfg, "schedules");
	if (!s)
		return false;

	size_t x = 0;
	HalonConfig* d;
	while ((d = HalonMTA_config_array_get(s, x++)))
	{
		const char* class_ = HalonMTA_config_string_get(HalonMTA_config_object_get(d, "class"), nullptr);
		const char* interval_ = HalonMTA_config_string_get(HalonMTA_config_object_get(d, "interval"), nullptr);
		auto s = HalonMTA_config_object_get(d, "schedule");
		if (!class_ || !s)
		{
			syslog(LOG_CRIT, "No class or schedule");
			return false;
		}

		schedule_t schedule;

		size_t l = 0;
		HalonConfig* i;

		HalonConfig* f = HalonMTA_config_object_get(d, "fields");
		if (f)
		{
			schedule.fields = 0;
			while ((i = HalonMTA_config_array_get(f, l++)))
			{
				for (auto m : (std::map<int, const char*>){
						{ HALONMTA_QUEUE_TRANSPORTID, "transportid" },
						{ HALONMTA_QUEUE_LOCALIP, "localip" },
						{ HALONMTA_QUEUE_REMOTEIP, "remoteip" },
						{ HALONMTA_QUEUE_REMOTEMX, "remotemx" },
						{ HALONMTA_QUEUE_RECIPIENTDOMAIN, "domain" },
						{ HALONMTA_QUEUE_JOBID, "jobid" }
					})
				{
					auto o = HalonMTA_config_object_get(i, m.second);
					if (o)
					{
						schedule.fields |= m.first;
						schedule.if_[m.first] = HalonMTA_config_string_get(o, nullptr);
					}
					else
					{
						if (strcmp(HalonMTA_config_string_get(i, nullptr), m.second) == 0)
							schedule.fields |= m.first;
					}
				}
			}
		}

		l = 0;
		while ((i = HalonMTA_config_array_get(s, l++)))
		{
			const char* day = HalonMTA_config_string_get(HalonMTA_config_object_get(i, "day"), nullptr);
			const char* messages = HalonMTA_config_string_get(HalonMTA_config_object_get(i, "messages"), nullptr);
			const char* interval = HalonMTA_config_string_get(HalonMTA_config_object_get(i, "interval"), nullptr);
			if (!day || !messages)
			{
				syslog(LOG_CRIT, "No day or messages");
				return false;
			}

			rate_t r;
			r.messages = strtoul(messages, nullptr, 10);
			r.interval = interval ? strtod(interval, nullptr) : interval_ ? strtod(interval_, nullptr) : 3600;
			schedule.days[strtoul(day, nullptr, 10)] = r;
		}

		if (schedule.days.empty())
			return false;

		schedules[class_] = schedule;
	}

	if (schedules.empty())
		return false;

	return true;
}

bool parseConfigIPs(HalonConfig* cfg, const std::map<std::string, schedule_t>& schedules, std::list<ip_t>& ips)
{
	auto s = HalonMTA_config_object_get(cfg, "ips");
	if (!s)
		return false;

	size_t l = 0;
	HalonConfig* d;
	while ((d = HalonMTA_config_array_get(s, l++)))
	{
		const char* ip = HalonMTA_config_string_get(HalonMTA_config_object_get(d, "ip"), nullptr);
		const char* helo = HalonMTA_config_string_get(HalonMTA_config_object_get(d, "helo"), nullptr);
		const char* class_ = HalonMTA_config_string_get(HalonMTA_config_object_get(d, "class"), nullptr);
		const char* added = HalonMTA_config_string_get(HalonMTA_config_object_get(d, "added"), nullptr);
		if (!ip || !class_ || !added)
		{
			syslog(LOG_CRIT, "Missing field in ip");
			return false;
		}

		if (schedules.find(class_) == schedules.end())
		{
			syslog(LOG_CRIT, "No schedule for class %s", class_);
			return false;
		}

		struct tm tm;
		memset(&tm, 0, sizeof (tm));
		strptime(added, "%Y-%m-%d", &tm);

		ip_t r;
		r.ip = ip;
		r.helo = helo ? helo : "";
		r.class_ = class_;
		r.added = mktime(&tm);
		ips.push_back(r);
	}
	return true;
}
