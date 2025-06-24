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

### How it works

The configuration provides a warmup schedule that is independent of config reloads and service restarts.

The current host date/time is compared against the IPs `added` property, to select which `day` applies. The `messages` property specifies the volume which will be attempted on the named warmup `class` over the defined `interval`.

So, in the above example, `day: 3` begins at `00:00` on `2021-03-13` (server locale) and ends when the server date becomes `2021-03-14`. Each localip will be used for delivery attempts to destinations belonging to grouping `&google` for _up to_ 40 messages per hour.

To reach this target volume, there needs to be sufficient messages in queue that are eligible for delivery.

Messages over the target volume can overflow to other addresses defined in the transport.

If IP selection is done in your configuration via the usual `transports` and `addresses` method, then no HSL work is needed. If you wish to customize IP selection (in pre-delivery HSL script) then the below function can be used to obtain IPs.

The warmup configuration can be defined before the IP(s) are added to active transports, ensuring a controlled start to the traffic from those IP(s).

## Exported functions

These functions needs to be [imported](https://docs.halon.io/hsl/structures.html#import) from the `extras://warmup` module path.

### warmup_ips([class])

Get all the warmup IP-addresses or only those for a specific class.

**Params**

- class `string` - The class

**Returns**

An array of the warmup IP-addresses.

Any `properties` are defined in the warmup class or day schedule are available in the returned result.
