package main

import (
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
)

func reset(t *testing.T) {
	t.Helper()
	storeDir = t.TempDir()
	mu.Lock()
	counts = map[string]int{}
	dayTotal = map[string]int{}
	mu.Unlock()
}

func post(body string) *httptest.ResponseRecorder {
	rr := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPost, "/xannouncer/report", strings.NewReader(body))
	req.RemoteAddr = "203.0.113.7:5555"
	handleReport(rr, req)
	return rr
}

func TestAcceptsReportAndReturnsID(t *testing.T) {
	reset(t)
	rr := post(`{"plugin":"2.0.0","aircraft":"A20N","log":"X-Announcer2: phase -> CLIMB"}`)
	if rr.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200 (%s)", rr.Code, rr.Body.String())
	}
	var got map[string]string
	if err := json.Unmarshal(rr.Body.Bytes(), &got); err != nil {
		t.Fatalf("response is not JSON: %v", err)
	}
	if !strings.HasPrefix(got["id"], "XA-") {
		t.Fatalf("id = %q, want an XA- ticket", got["id"])
	}

	// the report must be on disk, under today's directory, readable by no one else
	var found string
	filepath.Walk(storeDir, func(p string, fi os.FileInfo, err error) error {
		if err == nil && !fi.IsDir() && strings.HasSuffix(p, ".json") {
			found = p
			if m := fi.Mode().Perm(); m != 0o600 && m != 0o666 { // Windows reports 0666
				t.Errorf("stored file mode = %v, want owner-only", m)
			}
		}
		return nil
	})
	if found == "" {
		t.Fatal("nothing was written to the store")
	}
	blob, _ := os.ReadFile(found)
	if !strings.Contains(string(blob), "phase -> CLIMB") {
		t.Error("stored file does not contain the log it was sent")
	}
	if !strings.Contains(string(blob), "203.0.113.7") {
		t.Error("stored file must record where the report came from")
	}
}

// An empty report is a bug in the sender, not a report; taking it would fill the
// store with files nobody can act on.
func TestRejectsEmptyAndMalformed(t *testing.T) {
	reset(t)
	for _, body := range []string{`{}`, `{"log":"   "}`, `not json`, ``} {
		if rr := post(body); rr.Code != http.StatusBadRequest {
			t.Errorf("body %q: status = %d, want 400", body, rr.Code)
		}
	}
}

func TestRejectsOversizedBody(t *testing.T) {
	reset(t)
	huge := `{"log":"` + strings.Repeat("x", maxBody+64) + `"}`
	if rr := post(huge); rr.Code != http.StatusRequestEntityTooLarge {
		t.Fatalf("status = %d, want 413", rr.Code)
	}
}

// The endpoint is unauthenticated, so the only thing standing between it and a
// full disk is this counter.
func TestPerIPDailyLimit(t *testing.T) {
	reset(t)
	good := `{"log":"X-Announcer2: hello"}`
	for i := 0; i < perIPPerDay; i++ {
		if rr := post(good); rr.Code != http.StatusOK {
			t.Fatalf("report %d: status = %d, want 200", i+1, rr.Code)
		}
	}
	if rr := post(good); rr.Code != http.StatusTooManyRequests {
		t.Fatalf("status after the limit = %d, want 429", rr.Code)
	}
	// a different sender is unaffected
	rr := httptest.NewRecorder()
	req := httptest.NewRequest(http.MethodPost, "/xannouncer/report", strings.NewReader(good))
	req.RemoteAddr = "198.51.100.4:1111"
	handleReport(rr, req)
	if rr.Code != http.StatusOK {
		t.Fatalf("another IP got %d, want 200 — the limit must be per sender", rr.Code)
	}
}

// Rejected attempts must not consume the quota: otherwise a broken sender locks
// the user out of ever reporting the very bug they hit.
func TestRejectsDoNotConsumeQuota(t *testing.T) {
	reset(t)
	for i := 0; i < perIPPerDay*3; i++ {
		post(`{}`)
	}
	if rr := post(`{"log":"X-Announcer2: hello"}`); rr.Code != http.StatusOK {
		t.Fatalf("status = %d, want 200 — bad requests must not count", rr.Code)
	}
}

func TestOnlyPost(t *testing.T) {
	reset(t)
	rr := httptest.NewRecorder()
	handleReport(rr, httptest.NewRequest(http.MethodGet, "/xannouncer/report", nil))
	if rr.Code != http.StatusMethodNotAllowed {
		t.Fatalf("GET status = %d, want 405", rr.Code)
	}
}

// A spoofed X-Forwarded-For must not end up in the store as if it were fact —
// and must not smuggle anything into the file either.
func TestClientIPIgnoresGarbage(t *testing.T) {
	req := httptest.NewRequest(http.MethodPost, "/xannouncer/report", nil)
	req.RemoteAddr = "203.0.113.7:5555"
	req.Header.Set("X-Forwarded-For", "not-an-ip\nInjected: yes")
	if got := clientIP(req); got != "203.0.113.7" {
		t.Fatalf("clientIP = %q, want the socket address when the header is junk", got)
	}
	req.Header.Set("X-Forwarded-For", "198.51.100.9, 10.0.0.1")
	if got := clientIP(req); got != "198.51.100.9" {
		t.Fatalf("clientIP = %q, want the first hop", got)
	}
}
