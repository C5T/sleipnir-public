# sleipnir-public

Public demos of high-performance OPA implementation.

## Ubuntu

The examples can be run on Ubuntu. Tested on AWS EC2.

### Setup

#### OPA

```
# Via https://github.com/open-policy-agent/opa/releases
wget https://github.com/open-policy-agent/opa/releases/download/v0.42.2/opa_linux_amd64
chmod +x opa_linux_amd64 
sudo mv opa_linux_amd64 /usr/local/bin/opa
```

#### Docker & C++

```
sudo apt-get update
sudo apt install -y g++ golang-go
sudo apt install -y docker.io
sudo usermod -aG docker $USER
# Re-login to this EC2 instance for `docker` to work as this Linux user.
```

#### Current

```
git clone https://github.com/C5T/current
git clone https://github.com/dkorolev/dperftest
g++ -O3 -DNDEBUG -pthread dperftest/dperftest.cc -Icurrent -o dpt
```

#### Helpers

```
git clone https://github.com/C5T/sleipnir-public
for i in sleipnir-public/src/*.cc ; do
  g++ -O3 -DNDEBUG -pthread -std=c++17 -I. $i -o $(basename ${i/.cc/})
done
```

### Run

OPA:

```
opa run --server --log-level error
```

OPA on a single CPU core:

```
GOMAXPROCS=1 opa run --server --log-level error
```

Push the policy into OPA:

```
GH=https://raw.githubusercontent.com ; \
curl -s $GH/C5T/asbyrgi/main/tests/rbac_example/self_contained/rbac_example.rego \
| curl -X PUT --data-binary @/dev/stdin localhost:8181/v1/policies/test
```

Run a test query against OPA:

```
curl \
  -H 'Content-Type: application/json' \
  -d '{"input":{"user":"alice","action":"read","object":"server123"}}' \
  localhost:8181/v1/data/rbac/allow
```

Generate 100K test queries:

```
# If node.js is installed:
node sleipner-public/src/gen_example_queries.js >queries.txt

# Alternatively:
docker run crnt/sleipnir example_queries >queries.txt
```

Run a performance test for the first time (against the true OPA with the right policy):

```
./dpt \
  --url localhost:8181/v1/data/rbac/allow \
  --queries queries.txt \
  --write_goldens goldens.txt \
  --content_type application/json
```

Next runs, to compare against the goldens too:

```
./dpt \
  --url localhost:8181/v1/data/rbac/allow \
  --queries queries.txt \
  --goldens goldens.txt \
  --content_type application/json
```

Run a dummy Go HTTP server:

```
go run sleipnir-public/src/dummy_http.go
```

Run a dummy Go HTTP server on a single CPU core:

```
GOMAXPROCS=1 go run sleipnir-public/src/dummy_http.go
```

Run C++ test binaries:

```
./dummy_http                # The "echo server".
./manual_policy_impl        # Manual policy implementation.
./manual_policy_norun_impl  # HTTP + JSON, no policy eval.
```

### Transpilation

```
# The `rego2cc` target generates a complete C++ piece of code.
# Other targets: `rego2dsl`, `rego2h`.
GH=https://raw.githubusercontent.com ; \
curl -s $GH/C5T/asbyrgi/main/tests/rbac_example/self_contained/rbac_example.rego \
| docker run -i crnt/sleipnir rego2cc rbac allow >transpiled.cc
```

Since the transpiled sources are also part of the `src/` directory, then can be run with:

```
./transpiled -p 8181 -d
./transpiled_strongly_typed -p 8181 -d
```

The `-p 8181` option makes the binary respond via HTTP in a way identical to OPA. And `-d` stands for "daemonize", to run the HTTP server until terminated; the default behavior is to respond to queries from the standard input, and terminate as the EOF is read.

Or, to measure PAPS:

```
./transpiled --queries queries.txt
./transpiled_strongly_typed --queries queries.txt
```

The commands with `-p 8181` start a server on `localhost:8181`, identical to OPA wrt the policy evaluation endpoint.
