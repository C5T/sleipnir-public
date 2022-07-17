package main

import (
  "fmt"
  "net/http"
  "encoding/json"
)

type Response struct {
  Result bool `json:"result"`
}

func HandlerACL(w http.ResponseWriter, req *http.Request) {
  response := Response{false}
  w.Header().Set("Content-Type", "application/json")
  json.NewEncoder(w).Encode(response)
}

func main() {
  http.HandleFunc("/v1/data/rbac/allow", HandlerACL)
  fmt.Println("Go server listening on localhost:8181")
  http.ListenAndServe(":8181", nil)
}
