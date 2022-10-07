# IP warmup plugin

The IP warmup plugin can be used to schedule / ramp up traffic on newly added IPs.
Depending on the age (```added``` property) and the ```schedules``` traffic volumes are automatically calculated and controlled using dynamic policies.

## Installation

Follow the [instructions](https://docs.halon.io/manual/comp_install.html#installation) in our manual to add our package repository and then run the below command.

### Ubuntu

```
apt-get install halon-extras-warmup
```

### RHEL

```
yum install halon-extras-warmup
```

## Configuration

The schedule can be specified in both smtpd.yaml or smtpd-app.yaml for your convenience.
The default field is localip.

A example warmup only applying to the google mx'es.

### smtpd.yaml

```
plugins:
  - id: warmup
    config:
      schedules:
        - class: slow_warmup
          fields:
            - localip
            - remotemx: "#google"
          interval: 3600
          schedule:
            - day: 0
              messages: 10
            - day: 1
              messages: 20
            - day: 2
              messages: 30
            - day: 3
              messages: 40
```

### smtpd-app.yaml

The same IP can be added to multiple classes.

```
plugins:
  - id: warmup
    config:
      ips:
        - ip: 10.1.1.1
          class: slow_warmup
          added: "2021-03-10"
```

### smtpd-policy.yaml

A example policy with google mx'es grouped.

```
- fields:
  - localip
  - remotemx:
      google:
      - '*.gmail.com'
      - '*.google.com'
```

### Pre-delivery script hook

In the pre-delivery context, make use to configured IP's (may also be used to filter a specific class ```warmup_ips([class])```).

```
import { warmup_ips } from "extras://warmup";

Try(
	[
		"sourceip" => warmup_ips(),
		"sourceip_random" => false,
	]
);
```

## Exported functions

### warmup_ips([class])

Get all the warmup IP-addresses or only those for a specific class.

**Params**

- class `string` - The class

**Returns**

An array of the warmup IP-addresses.
