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

The default field is `localip`. The same `ip` can be added to multiple classes.

Below is an example where warmup is only applied for the google `grouping`.

### smtpd-app.yaml

```
plugins:
  - id: warmup
    config:
      schedules:
        - class: slow_warmup
          fields:
            - localip
            - grouping: "&google"
          interval: 3600
          properties:
            foo: bar
          schedule:
            - day: 0
              messages: 10
              properties:
                foo: bar2
            - day: 1
              messages: 20
            - day: 2
              messages: 30
            - day: 3
              messages: 40
      ips:
        - ip: 10.1.1.1
          class: slow_warmup
          added: "2021-03-10"
```

### smtpd-policy.yaml

```
policies:
  - fields:
    - localip
    - grouping
```

## Exported functions

These functions needs to be [imported](https://docs.halon.io/hsl/structures.html#import) from the `extras://warmup` module path.

### warmup_ips([class])

Get all the warmup IP-addresses or only those for a specific class.

**Params**

- class `string` - The class

**Returns**

An array of the warmup IP-addresses.
