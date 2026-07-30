// xa-reports: the drop box for X-Announcer log reports.
//
// The plugin sends a report only when a human presses "Отправить журнал", so
// this endpoint is unauthenticated by necessity: the sender is a stranger with
// a broken sim, not a logged-in member. Everything here is therefore written
// against abuse rather than against mistakes — a hard body cap, a per-IP
// bucket, a daily ceiling, and a store that only ever grows by whole files.
//
// It never serves reports back. Reading them is an ssh job; an endpoint that
// hands out other people's logs is exactly the thing this must not become.
package main

import (
	"crypto/rand"
	"encoding/hex"
	"encoding/json"
	"io"
	"log"
	"net"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"
)

const (
	maxBody     = 1 << 20 // 1 MB: the plugin trims to ~256 KB, this is the wall
	perIPPerDay = 12      // one person debugging their sim, not a firehose
	globalDay   = 300     // ceiling for the whole service
	retainDays  = 90
)

var (
	storeDir = envOr("STORE_DIR", "/var/lib/xa-reports")
	listen   = envOr("LISTEN", "127.0.0.1:8092")

	mu      sync.Mutex
	counts  = map[string]int{} // "YYYY-MM-DD|ip" -> accepted today
	dayTotal = map[string]int{}
)

// report is what the plugin sends. Every field is optional: a report that
// arrives half-filled because the sim was already dying is still worth having.
type report struct {
	Plugin   string `json:"plugin"`
	XPlane   string `json:"xplane"`
	OS       string `json:"os"`
	Aircraft string `json:"aircraft"`
	Pack     string `json:"pack"`
	Note     string `json:"note"`
	Settings string `json:"settings"`
	Log      string `json:"log"`
}

func main() {
	if err := os.MkdirAll(storeDir, 0o700); err != nil {
		log.Fatalf("store dir: %v", err)
	}
	prune()
	go func() {
		for range time.Tick(6 * time.Hour) {
			prune()
		}
	}()

	mux := http.NewServeMux()
	mux.HandleFunc("/xannouncer/report", handleReport)
	mux.HandleFunc("/xannouncer/report/health", func(w http.ResponseWriter, r *http.Request) {
		io.WriteString(w, "ok")
	})

	srv := &http.Server{
		Addr:              listen,
		Handler:           mux,
		ReadHeaderTimeout: 10 * time.Second,
		ReadTimeout:       60 * time.Second,
		WriteTimeout:      20 * time.Second,
	}
	log.Printf("xa-reports on %s, store %s", listen, storeDir)
	log.Fatal(srv.ListenAndServe())
}

func handleReport(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		w.Header().Set("Allow", "POST")
		httpError(w, http.StatusMethodNotAllowed, "method")
		return
	}
	body, err := io.ReadAll(io.LimitReader(r.Body, maxBody+1))
	if err != nil {
		httpError(w, http.StatusBadRequest, "read")
		return
	}
	if len(body) > maxBody {
		httpError(w, http.StatusRequestEntityTooLarge, "too big")
		return
	}
	var rep report
	if err := json.Unmarshal(body, &rep); err != nil {
		httpError(w, http.StatusBadRequest, "json")
		return
	}
	if strings.TrimSpace(rep.Log) == "" && strings.TrimSpace(rep.Note) == "" {
		httpError(w, http.StatusBadRequest, "empty")
		return
	}
	ip := clientIP(r)
	if !take(ip, today()) {
		httpError(w, http.StatusTooManyRequests, "rate")
		return
	}
	id, err := store(rep, ip)
	if err != nil {
		log.Printf("store: %v", err)
		httpError(w, http.StatusInternalServerError, "store")
		return
	}
	log.Printf("report %s from %s (%d bytes, plugin %q, aircraft %q)",
		id, ip, len(body), clip(rep.Plugin, 32), clip(rep.Aircraft, 16))
	writeJSON(w, http.StatusOK, map[string]string{"id": id})
}

// take is the rate limiter: per-IP and global, both per calendar day. Counting
// only ACCEPTED reports means a burst of rejects can't lock a person out.
func take(ip, day string) bool {
	mu.Lock()
	defer mu.Unlock()
	if dayTotal[day] >= globalDay {
		return false
	}
	key := day + "|" + ip
	if counts[key] >= perIPPerDay {
		return false
	}
	counts[key]++
	dayTotal[day]++
	// yesterday's tallies are dead weight
	for k := range counts {
		if !strings.HasPrefix(k, day+"|") {
			delete(counts, k)
		}
	}
	for k := range dayTotal {
		if k != day {
			delete(dayTotal, k)
		}
	}
	return true
}

// store writes the report as one file. The id is what the user quotes in
// Discord, so it must be short, unambiguous out loud, and unguessable enough
// that the files can't be enumerated if the directory is ever exposed.
func store(rep report, ip string) (string, error) {
	day := today()
	id := "XA-" + strings.ReplaceAll(day, "-", "") + "-" + randHex(3)
	dir := filepath.Join(storeDir, day)
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return "", err
	}
	envelope := map[string]any{
		"id":       id,
		"received": time.Now().UTC().Format(time.RFC3339),
		"ip":       ip,
		"plugin":   clip(rep.Plugin, 64),
		"xplane":   clip(rep.XPlane, 64),
		"os":       clip(rep.OS, 64),
		"aircraft": clip(rep.Aircraft, 32),
		"pack":     clip(rep.Pack, 32),
		"note":     clip(rep.Note, 4000),
		"settings": clip(rep.Settings, 8000),
		"log":      rep.Log,
	}
	// Not MarshalIndent: it escapes < > &, and a stored log full of > is a
	// log nobody wants to read. These files are read by a human over ssh.
	var buf strings.Builder
	enc := json.NewEncoder(&buf)
	enc.SetEscapeHTML(false)
	enc.SetIndent("", "  ")
	if err := enc.Encode(envelope); err != nil {
		return "", err
	}
	blob := []byte(buf.String())
	tmp := filepath.Join(dir, id+".json.part")
	if err := os.WriteFile(tmp, blob, 0o600); err != nil {
		return "", err
	}
	if err := os.Rename(tmp, filepath.Join(dir, id+".json")); err != nil {
		return "", err
	}
	return id, nil
}

// prune drops day-directories older than retainDays. Reports are debugging
// aids, not records; keeping them forever would only grow the thing worth
// stealing.
func prune() {
	entries, err := os.ReadDir(storeDir)
	if err != nil {
		return
	}
	cutoff := time.Now().AddDate(0, 0, -retainDays)
	names := make([]string, 0, len(entries))
	for _, e := range entries {
		if e.IsDir() {
			names = append(names, e.Name())
		}
	}
	sort.Strings(names)
	for _, name := range names {
		d, err := time.Parse("2006-01-02", name)
		if err != nil || !d.Before(cutoff) {
			continue
		}
		if err := os.RemoveAll(filepath.Join(storeDir, name)); err != nil {
			log.Printf("prune %s: %v", name, err)
			continue
		}
		log.Printf("pruned %s", name)
	}
}

func today() string { return time.Now().UTC().Format("2006-01-02") }

func clip(s string, n int) string {
	if len(s) <= n {
		return s
	}
	return s[:n]
}

func clientIP(r *http.Request) string {
	if f := r.Header.Get("X-Forwarded-For"); f != "" {
		if i := strings.IndexByte(f, ','); i >= 0 {
			f = f[:i]
		}
		if ip := net.ParseIP(strings.TrimSpace(f)); ip != nil {
			return ip.String()
		}
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func httpError(w http.ResponseWriter, code int, reason string) {
	writeJSON(w, code, map[string]string{"error": reason})
}

func writeJSON(w http.ResponseWriter, code int, v any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(code)
	json.NewEncoder(w).Encode(v)
}

func randHex(n int) string {
	b := make([]byte, n)
	rand.Read(b)
	return strings.ToUpper(hex.EncodeToString(b))
}

func envOr(k, def string) string {
	if v := os.Getenv(k); v != "" {
		return v
	}
	return def
}
