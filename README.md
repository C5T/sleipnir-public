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
