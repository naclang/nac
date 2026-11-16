// Go reference servers for bench/run.js.
//
//   go build -o build/bench-server ./bench/go
//   MODE=hello PORT=8080 build/bench-server
//   MODE=json PORT=8081 build/bench-server
//   MODE=static PORT=8082 STATIC_DIR=bench/nac/public build/bench-server
package main

import (
	"encoding/json"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

var mime = map[string]string{
	".html": "text/html; charset=utf-8",
	".css":  "text/css; charset=utf-8",
	".js":   "text/javascript; charset=utf-8",
	".json": "application/json",
	".txt":  "text/plain; charset=utf-8",
	".png":  "image/png",
	".jpg":  "image/jpeg",
	".svg":  "image/svg+xml",
	".ico":  "image/x-icon",
}

func send(w http.ResponseWriter, status int, contentType string, body []byte) {
	w.Header().Set("Content-Type", contentType)
	w.Header().Set("Content-Length", strconv.Itoa(len(body)))
	w.Header().Set("Connection", "close")
	w.WriteHeader(status)
	w.Write(body)
}

func main() {
	mode := os.Getenv("MODE")
	if mode == "" {
		mode = "hello"
	}
	port := os.Getenv("PORT")
	if port == "" {
		port = "8080"
	}
	staticDir := os.Getenv("STATIC_DIR")
	if staticDir == "" {
		staticDir = "bench/nac/public"
	}
	staticDir, _ = filepath.Abs(staticDir)

	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		switch mode {
		case "hello":
			send(w, 200, "text/plain; charset=utf-8", []byte("Hello, World!"))

		case "json":
			if r.Method == http.MethodPost && r.URL.Path == "/json" {
				body, _ := io.ReadAll(r.Body)
				var value interface{}
				if err := json.Unmarshal(body, &value); err != nil {
					send(w, 400, "application/json", []byte(`{"error":"invalid json"}`))
					return
				}
				send(w, 200, "application/json", body)
				return
			}
			send(w, 200, "application/json", []byte(`{"message":"Hello, World!","ok":true}`))

		case "static":
			if r.Method != http.MethodGet && r.Method != http.MethodHead {
				send(w, 405, "text/plain", []byte("method not allowed"))
				return
			}
			rel := strings.TrimPrefix(r.URL.Path, "/")
			if rel == "" {
				rel = "index.html"
			}
			file := filepath.Join(staticDir, filepath.FromSlash(rel))
			data, err := os.ReadFile(file)
			if err != nil {
				send(w, 404, "text/plain", []byte("not found"))
				return
			}
			ext := strings.ToLower(filepath.Ext(file))
			contentType := mime[ext]
			if contentType == "" {
				contentType = "application/octet-stream"
			}
			send(w, 200, contentType, data)

		default:
			send(w, 404, "text/plain", []byte("unknown MODE"))
		}
	})

	println("go " + mode + " server on http://127.0.0.1:" + port + "/")
	http.ListenAndServe("127.0.0.1:"+port, nil)
}
