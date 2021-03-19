#include <HalonMTA.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <thread>
#include <syslog.h>
#include <list>
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
	std::string class_;
	time_t added;
};

std::map<std::string, std::map<size_t, rate_t>> schedules;
std::list<ip_t> ips;
std::mutex lock;
std::map<std::string, std::pair<std::pair<size_t, double>, char*>> policies;

bool stop = false;
std::thread p;

bool parseConfigSchedule(HalonConfig* cfg, std::map<std::string, std::map<size_t, rate_t>>& schedules);
bool parseConfigIPs(HalonConfig*, const std::map<std::string, std::map<size_t, rate_t>>& schedules, std::list<ip_t>&);
void update_rates();

HALON_EXPORT
bool Halon_init(HalonInitContext* hic)
{
	HalonConfig *cfg, *app;
	HalonMTA_init_getinfo(hic, HALONMTA_INIT_CONFIG, nullptr, 0, &cfg, nullptr);
	HalonMTA_init_getinfo(hic, HALONMTA_INIT_APPCONFIG, nullptr, 0, &app, nullptr);

	if (!parseConfigSchedule(app, schedules))
		if (!parseConfigSchedule(cfg, schedules))
			return false;

	if (!parseConfigIPs(app, schedules, ips))
		return false;

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

		std::map<std::string, std::map<size_t, rate_t>> schedules2;
		if (parseConfigSchedule(hc, schedules2))
			schedules = schedules2;

		std::list<ip_t> ips2;
		if (parseConfigIPs(hc, schedules, ips2))
			ips = ips2;

		for (auto it = policies.begin(); it != policies.end();)
		{
			bool found = false;
			for (const auto & ip : ips)
			{
				if (ip.ip == it->first)
				{
					found = true;
					break;
				}
			}
			if (found)
				++it;
			else
			{
				if (it->second.second)
				{
					HalonMTA_queue_policy_delete(it->second.second);
					free(it->second.second);
					syslog(LOG_INFO, "WarmUP: untrack ip %s", it->first.c_str());
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
		auto ip = ip_.ip;
		ssize_t days_ = now < ip_.added ? 0 : (now - ip_.added) / (3600 * 24);

		size_t messages = 0;
		double interval = 0;
		size_t days = 0;

		for (auto s = schedules[ip_.class_].rbegin(); s != schedules[ip_.class_].rend(); s++)
		{
			if (days_ > s->first)
				break;
			days = s->first;
			messages = s->second.messages;
			interval = s->second.interval;
		}

		auto it = policies.find(ip);
		if (it == policies.end())
		{
			if (messages > 0)
			{
				auto p = HalonMTA_queue_policy_add(HALONMTA_QUEUE_LOCALIP, nullptr, ip.c_str(), nullptr, nullptr, nullptr, nullptr, 0, messages, interval, std::string(std::string("Day_") + std::to_string(days)).c_str(), 0);

				policies[ip] = { { messages, interval }, p };
				syslog(LOG_INFO, "WarmUP: ip:%s class:%s days:%ld rate:%zu/%f", ip.c_str(), ip_.class_.c_str(), days, messages, interval);
			}
		}
		else
		{
			if (messages <= 0)
			{
				HalonMTA_queue_policy_delete(it->second.second);
				free(it->second.second);
				syslog(LOG_INFO, "WarmUP: ip:%s class:%s days:%ld (no limit)", ip.c_str(), ip_.class_.c_str(), days_);
				policies.erase(ip);
			}
			else if (it->second.first.first != messages || it->second.first.second != interval)
			{
				HalonMTA_queue_policy_update(it->second.second, 0, messages, interval, std::string(std::string("Day_") + std::to_string(days)).c_str(), 0);
				syslog(LOG_INFO, "WarmUP: ip:%s class:%s days:%ld rate:%zu/%f->%zu/%f", ip.c_str(), ip_.class_.c_str(), days, it->second.first.first, it->second.first.second, messages, interval);
				it->second.first.first = messages;
				it->second.first.second = interval;
			}
		}
	}
}

bool parseConfigSchedule(HalonConfig* cfg, std::map<std::string, std::map<size_t, rate_t>>& schedules)
{
	auto s = HalonMTA_config_object_get(cfg, "schedules");
	if (!s)
	{
		syslog(LOG_CRIT, "No schedules");
		return false;
	}

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

		std::map<size_t, rate_t> schedule;

		size_t l = 0;
		HalonConfig* i;
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
			schedule[strtoul(day, nullptr, 10)] = r;
		}

		if (schedule.empty())
			return false;

		schedules[class_] = schedule;
	}

	if (schedules.empty())
		return false;

	return true;
}

bool parseConfigIPs(HalonConfig* cfg, const std::map<std::string, std::map<size_t, rate_t>>& schedules, std::list<ip_t>& ips)
{
	auto s = HalonMTA_config_object_get(cfg, "ips");
	if (!s)
		return false;

	size_t l = 0;
	HalonConfig* d;
	while ((d = HalonMTA_config_array_get(s, l++)))
	{
		const char* ip = HalonMTA_config_string_get(HalonMTA_config_object_get(d, "ip"), nullptr);
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
		r.class_ = class_;
		r.added = mktime(&tm);
		ips.push_back(r);
	}
	return true;
}
