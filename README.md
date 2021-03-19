## IP warmup plugin

The IP warmup plugin can be used to schedule / ramp up traffic on newly added IPs.
Depending on the age (```added``` property) and the ```schedules``` traffic volumes are automatically calculated.

### Examples

The schedule can be specified in both smtpd.yaml or smtpd-app.yaml for your convenience.

The schedule config in smtpd.yaml

```
schedules:
  - class: slow_warmup
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

The plugin config in smtpd-app.yaml

```
ips:
  - ip: 10.1.1.1
    class: slow_warmup
    added: 2021-03-10
```
