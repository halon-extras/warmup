# Build instructions

```
export HALON_REPO_USER=exampleuser
export HALON_REPO_PASS=examplepass
docker compose -p halon-extras-warmup --profile all up --build
docker compose -p halon-extras-warmup down --rmi local
```
