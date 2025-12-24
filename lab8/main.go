package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/url"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

type Header struct {
	Name  string
	Value string
}

type Request struct {
	Method    string
	URI       string
	Version   string
	Headers   []Header
	HeaderMap map[string]string
	Body      []byte
}

func main() {
	host := flag.String("host", "0.0.0.0", "listen host")
	port := flag.Int("port", 8080, "listen port")
	mode := flag.String("mode", "full", "mode: parse|echo|map|full")
	root := flag.String("root", "", "assets root (default: assets or ../assets)")
	user := flag.String("user", "test", "login username")
	pass := flag.String("pass", "test", "login password")
	flag.Parse()

	assetsRoot, err := resolveAssetsRoot(*root)
	if err != nil {
		log.Fatalf("assets root: %v", err)
	}

	addr := fmt.Sprintf("%s:%d", *host, *port)
	ln, err := net.Listen("tcp", addr)
	if err != nil {
		log.Fatalf("listen: %v", err)
	}
	log.Printf("lab8 server listening on %s (mode=%s, root=%s)", addr, *mode, assetsRoot)

	for {
		conn, err := ln.Accept()
		if err != nil {
			log.Printf("accept: %v", err)
			continue
		}
		go handleConn(conn, *mode, assetsRoot, *user, *pass)
	}
}

func resolveAssetsRoot(root string) (string, error) {
	if root != "" {
		if exists(root) {
			return root, nil
		}
		return "", fmt.Errorf("provided root not found: %s", root)
	}
	if exists("assets") {
		return "assets", nil
	}
	if exists(filepath.Join("..", "assets")) {
		return filepath.Join("..", "assets"), nil
	}
	return "", fmt.Errorf("assets directory not found")
}

func exists(path string) bool {
	info, err := os.Stat(path)
	return err == nil && info.IsDir()
}

func handleConn(conn net.Conn, mode, assetsRoot, user, pass string) {
	defer conn.Close()

	req, err := readRequest(conn)
	if err != nil {
		writeResponse(conn, "400 Bad Request", []Header{
			{Name: "Content-Length", Value: "0"},
		}, nil)
		return
	}

	switch mode {
	case "parse":
		handleStructureParse(conn, req)
	case "echo":
		handleWebEcho(conn, req)
	case "map":
		handleUriMapping(conn, req)
	case "full":
		handleFull(conn, req, assetsRoot, user, pass)
	default:
		writeResponse(conn, "400 Bad Request", []Header{
			{Name: "Content-Length", Value: "0"},
		}, nil)
	}
}

func readRequest(conn net.Conn) (*Request, error) {
	reader := bufio.NewReader(conn)
	raw, err := readUntilDoubleCRLF(reader, 1024*1024)
	if err != nil {
		return nil, err
	}

	headerEnd := bytes.Index(raw, []byte("\r\n\r\n"))
	if headerEnd < 0 {
		return nil, fmt.Errorf("header not complete")
	}
	headerPart := raw[:headerEnd]
	bodyRemainder := raw[headerEnd+4:]

	lines := strings.Split(string(headerPart), "\r\n")
	if len(lines) == 0 {
		return nil, fmt.Errorf("empty request")
	}

	method, uri, version, err := parseRequestLine(lines[0])
	if err != nil {
		return nil, err
	}

	headers := make([]Header, 0, len(lines)-1)
	headerMap := make(map[string]string)
	for _, line := range lines[1:] {
		if line == "" {
			continue
		}
		name, value, ok := splitHeaderLine(line)
		if !ok {
			continue
		}
		headers = append(headers, Header{Name: name, Value: value})
		headerMap[strings.ToLower(name)] = value
	}

	contentLength := 0
	if v, ok := headerMap["content-length"]; ok {
		if n, convErr := strconv.Atoi(strings.TrimSpace(v)); convErr == nil && n > 0 {
			contentLength = n
		}
	}

	body := bodyRemainder
	if contentLength > len(body) {
		more := make([]byte, contentLength-len(body))
		if _, err := io.ReadFull(reader, more); err != nil {
			return nil, err
		}
		body = append(body, more...)
	}
	if contentLength > 0 && len(body) > contentLength {
		body = body[:contentLength]
	}

	return &Request{
		Method:    method,
		URI:       uri,
		Version:   version,
		Headers:   headers,
		HeaderMap: headerMap,
		Body:      body,
	}, nil
}

func readUntilDoubleCRLF(reader *bufio.Reader, limit int) ([]byte, error) {
	var buf []byte
	for len(buf) < limit {
		part := make([]byte, 1024)
		n, err := reader.Read(part)
		if n > 0 {
			buf = append(buf, part[:n]...)
			if bytes.Contains(buf, []byte("\r\n\r\n")) {
				return buf, nil
			}
		}
		if err != nil {
			if err == io.EOF {
				return buf, nil
			}
			return nil, err
		}
	}
	return nil, fmt.Errorf("header too large")
}

func parseRequestLine(line string) (string, string, string, error) {
	parts := strings.SplitN(line, " ", 3)
	if len(parts) != 3 {
		return "", "", "", fmt.Errorf("invalid request line")
	}
	return parts[0], parts[1], parts[2], nil
}

func splitHeaderLine(line string) (string, string, bool) {
	parts := strings.SplitN(line, ":", 2)
	if len(parts) != 2 {
		return "", "", false
	}
	return strings.TrimSpace(parts[0]), strings.TrimSpace(parts[1]), true
}

func handleStructureParse(conn net.Conn, req *Request) {
	var bodyBuilder strings.Builder
	for _, h := range req.Headers {
		bodyBuilder.WriteString(h.Name)
		bodyBuilder.WriteString(": ")
		bodyBuilder.WriteString(h.Value)
		bodyBuilder.WriteString("\r\n")
	}
	bodyBuilder.WriteString(fmt.Sprintf("%s %s %s", req.Method, req.URI, req.Version))
	body := []byte(bodyBuilder.String())
	headers := []Header{
		{Name: "Content-Type", Value: "text/plain"},
		{Name: "Content-Length", Value: strconv.Itoa(len(body))},
	}
	writeResponse(conn, "200 OK", headers, body)
}

func handleWebEcho(conn net.Conn, req *Request) {
	headers := make([]Header, 0, len(req.Headers))
	headers = append(headers, req.Headers...)
	writeResponse(conn, "200 OK", headers, req.Body)
}

func handleUriMapping(conn net.Conn, req *Request) {
	method := strings.ToUpper(req.Method)
	uri := normalizePath(req.URI)

	if method == "GET" {
		if internal, ok := mapUri(uri); ok {
			body := []byte(internal)
			headers := []Header{
				{Name: "Content-Type", Value: "text/plain"},
				{Name: "Content-Length", Value: strconv.Itoa(len(body))},
			}
			writeResponse(conn, "200 OK", headers, body)
			return
		}
		writeResponse(conn, "404 Not Found", []Header{{Name: "Content-Length", Value: "0"}}, nil)
		return
	}

	if method == "POST" {
		if uri == "/dopost" {
			writeResponse(conn, "200 OK", []Header{{Name: "Content-Length", Value: "0"}}, nil)
			return
		}
		writeResponse(conn, "404 Not Found", []Header{{Name: "Content-Length", Value: "0"}}, nil)
		return
	}

	writeResponse(conn, "404 Not Found", []Header{{Name: "Content-Length", Value: "0"}}, nil)
}

func handleFull(conn net.Conn, req *Request, assetsRoot, user, pass string) {
	method := strings.ToUpper(req.Method)
	uri := normalizePath(req.URI)

	if method == "GET" {
		internal, ok := mapUri(uri)
		if !ok {
			writeResponse(conn, "404 Not Found", []Header{{Name: "Content-Length", Value: "0"}}, nil)
			return
		}
		filePath := filepath.Join(assetsRoot, filepath.FromSlash(strings.TrimPrefix(internal, "/")))
		data, err := os.ReadFile(filePath)
		if err != nil {
			writeResponse(conn, "404 Not Found", []Header{{Name: "Content-Length", Value: "0"}}, nil)
			return
		}
		contentType := contentTypeForExt(filepath.Ext(filePath))
		headers := []Header{
			{Name: "Content-Type", Value: contentType},
			{Name: "Content-Length", Value: strconv.Itoa(len(data))},
		}
		writeResponse(conn, "200 OK", headers, data)
		return
	}

	if method == "POST" {
		if uri != "/dopost" {
			writeResponse(conn, "404 Not Found", []Header{{Name: "Content-Length", Value: "0"}}, nil)
			return
		}
		message := loginMessage(req.Body, user, pass)
		body := []byte(fmt.Sprintf("<html><body>%s</body></html>", message))
		headers := []Header{
			{Name: "Content-Type", Value: "text/html"},
			{Name: "Content-Length", Value: strconv.Itoa(len(body))},
		}
		writeResponse(conn, "200 OK", headers, body)
		return
	}

	writeResponse(conn, "404 Not Found", []Header{{Name: "Content-Length", Value: "0"}}, nil)
}

func normalizePath(uri string) string {
	if uri == "" {
		return "/"
	}
	uri = strings.TrimSpace(uri)
	if !strings.HasPrefix(uri, "/") {
		uri = "/" + uri
	}
	parts := strings.SplitN(uri, "?", 2)
	path := parts[0]
	parts = strings.SplitN(path, "#", 2)
	return parts[0]
}

func mapUri(external string) (string, bool) {
	switch external {
	case "/index.html":
		return "/html/test.html", true
	case "/index_noimg.html":
		return "/html/noimg.html", true
	case "/info/server":
		return "/txt/test.txt", true
	case "/assets/logo.jpg":
		return "/img/logo.jpg", true
	default:
		return "", false
	}
}

func contentTypeForExt(ext string) string {
	switch strings.ToLower(ext) {
	case ".html":
		return "text/html"
	case ".txt":
		return "text/plain"
	case ".jpg", ".jpeg":
		return "image/jpeg"
	default:
		return "application/octet-stream"
	}
}

func loginMessage(body []byte, user, pass string) string {
	values, err := url.ParseQuery(string(body))
	if err != nil {
		return "Login Failed"
	}
	login := values.Get("login")
	password := values.Get("pass")
	if login == user && password == pass {
		return "Login Success"
	}
	return "Login Failed"
}

func writeResponse(conn net.Conn, status string, headers []Header, body []byte) {
	var buf bytes.Buffer
	buf.WriteString("HTTP/1.0 ")
	buf.WriteString(status)
	buf.WriteString("\r\n")
	for _, h := range headers {
		buf.WriteString(h.Name)
		buf.WriteString(": ")
		buf.WriteString(h.Value)
		buf.WriteString("\r\n")
	}
	buf.WriteString("\r\n")
	if len(body) > 0 {
		buf.Write(body)
	}
	_, _ = conn.Write(buf.Bytes())
}
