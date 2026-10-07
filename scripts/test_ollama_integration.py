#!/usr/bin/env python3
import http.server
import socketserver
import threading
import json
import time
import urllib.request
import urllib.error
import subprocess
import sys
import os

# 1. Mock Ollama Daemon
class MockOllamaHandler(http.server.BaseHTTPRequestHandler):
    chat_calls = []

    def do_GET(self):
        if self.path == "/api/version":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps({"version": "0.3.14"}).encode())
        elif self.path == "/api/tags":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            models = {
                "models": [
                    {
                        "name": "llama3:latest",
                        "model": "llama3:latest",
                        "modified_at": "2026-09-01T10:00:00Z",
                        "size": 4661224676,
                        "digest": "sha256:123456",
                        "details": {
                            "family": "llama",
                            "parameter_size": "8.0B",
                            "format": "gguf",
                            "quantization_level": "Q4_0"
                        }
                    },
                    {
                        "name": "mistral:latest",
                        "model": "mistral:latest",
                        "modified_at": "2026-09-15T12:00:00Z",
                        "size": 4109845000,
                        "digest": "sha256:789abc",
                        "details": {
                            "family": "mistral",
                            "parameter_size": "7.0B",
                            "format": "gguf",
                            "quantization_level": "Q4_K_M"
                        }
                    }
                ]
            }
            self.wfile.write(json.dumps(models).encode())
        elif self.path == "/api/ps":
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            running = {
                "models": [
                    {
                        "name": "llama3:latest",
                        "model": "llama3:latest",
                        "size": 4661224676
                    }
                ]
            }
            self.wfile.write(json.dumps(running).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def do_POST(self):
        content_len = int(self.headers.get("Content-Length", 0))
        post_body = self.rfile.read(content_len)
        data = json.loads(post_body.decode())

        if self.path == "/api/chat":
            MockOllamaHandler.chat_calls.append(data)
            messages = data.get("messages", [])
            last_msg = messages[-1]["content"] if messages else ""
            model = data.get("model", "llama3:latest")

            reply = f"Mocked reply from {model} for: '{last_msg}'. Prior history count: {len(messages) - 1}"
            resp = {
                "model": model,
                "created_at": "2026-10-07T12:00:00Z",
                "message": {
                    "role": "assistant",
                    "content": reply
                },
                "done": True,
                "total_duration": 150000000,
                "eval_count": 42
            }
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode())
        elif self.path == "/api/embed":
            inputs = data.get("input", [])
            if isinstance(inputs, str):
                inputs = [inputs]
            embeddings = [[0.01 * (i + 1), 0.02 * (i + 1), 0.03 * (i + 1)] for i in range(len(inputs))]
            resp = {
                "model": data.get("model", "llama3:latest"),
                "embeddings": embeddings,
                "total_duration": 50000000,
                "prompt_eval_count": len(inputs) * 5
            }
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode())
        elif self.path == "/api/embeddings":
            resp = {
                "embedding": [0.01, 0.02, 0.03]
            }
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.end_headers()
            self.wfile.write(json.dumps(resp).encode())
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        # suppress noisy http logs
        pass

class ReusableTCPServer(socketserver.TCPServer):
    allow_reuse_address = True

def run_test():
    # Start Mock Ollama on 11434
    ollama_server = ReusableTCPServer(("127.0.0.1", 11434), MockOllamaHandler)
    ollama_thread = threading.Thread(target=ollama_server.serve_forever)
    ollama_thread.daemon = True
    ollama_thread.start()
    print("[TEST] Mock Ollama server started on port 11434")

    # Start API server on port 8085
    server_proc = subprocess.Popen(
        ["./build/API-cli", "--no-dashboard", "--port", "8085"],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )
    print(f"[TEST] API-cli server launched (PID: {server_proc.pid})")
    time.sleep(1.5)

    try:
        def request(method, path, body=None, headers=None):
            url = f"http://127.0.0.1:8085{path}"
            req_headers = {"Content-Type": "application/json"}
            if headers:
                req_headers.update(headers)
            req = urllib.request.Request(
                url,
                data=json.dumps(body).encode() if body else None,
                headers=req_headers,
                method=method
            )
            try:
                with urllib.request.urlopen(req) as resp:
                    ct = resp.headers.get("Content-Type", "")
                    if "json" in ct:
                        return resp.status, json.loads(resp.read().decode())
                    return resp.status, resp.read().decode()
            except urllib.error.HTTPError as e:
                ct = e.headers.get("Content-Type", "")
                if "json" in ct:
                    return e.code, json.loads(e.read().decode())
                return e.code, e.read().decode()

        # Test 1: Check Ollama status
        status, data = request("GET", "/api/ollama/status")
        print(f"[TEST 1] /api/ollama/status -> HTTP {status}: {data}")
        assert status == 200
        assert data["status"] == "ok"
        assert data["version"] == "0.3.14"

        # Test 2: Auto-find agents + personas
        status, data = request("GET", "/api/agents")
        print(f"[TEST 2] /api/agents (auto find agents) -> HTTP {status}: {data['count']} agent(s) discovered, {len(data.get('personas', []))} personas")
        assert status == 200
        assert data["count"] == 2
        agent_names = [a["name"] for a in data["agents"]]
        assert "llama3:latest" in agent_names
        assert "mistral:latest" in agent_names
        llama = next(a for a in data["agents"] if a["name"] == "llama3:latest")
        assert llama["is_running"] == True
        assert llama["family"] == "llama"
        assert llama["parameter_size"] == "8.0B"
        # Check personas list
        persona_ids = [p["id"] for p in data.get("personas", [])]
        assert "coder" in persona_ids
        assert "assistant" in persona_ids
        assert "summarizer" in persona_ids

        # Test 3: Chat with user IP identification (IP 1: 192.168.1.101)
        ip1_headers = {"X-Forwarded-For": "192.168.1.101"}
        status, data = request("POST", "/api/chat", {"message": "Hello from IP1!"}, ip1_headers)
        print(f"[TEST 3] /api/chat (IP1 message 1) -> HTTP {status}: response='{data['response']}', agent='{data['agent']}'")
        assert status == 200
        assert data["user_ip"] == "192.168.1.101"
        assert data["agent"] == "llama3:latest" # auto-selected!
        assert len(data["chat_history"]) == 2 # 1 user, 1 assistant

        # Test 4: Second chat message from IP 1 (multi-turn history caching)
        status, data = request("POST", "/api/chat", {"message": "What was my first message?"}, ip1_headers)
        print(f"[TEST 4] /api/chat (IP1 message 2) -> HTTP {status}: response='{data['response']}'")
        assert status == 200
        assert len(data["chat_history"]) == 4 # 2 user, 2 assistant
        last_ollama_call = MockOllamaHandler.chat_calls[-1]
        assert len(last_ollama_call["messages"]) == 4

        # Test 5: Verify GET /api/chat/history for IP 1
        status, data = request("GET", "/api/chat/history", headers=ip1_headers)
        print(f"[TEST 5] /api/chat/history (IP1) -> HTTP {status}: {data['message_count']} messages")
        assert status == 200
        assert data["user_ip"] == "192.168.1.101"
        assert data["message_count"] == 4
        assert data["has_session"] == True

        # Test 6: Verify session isolation with IP 2 (192.168.1.202)
        ip2_headers = {"X-Forwarded-For": "192.168.1.202"}
        status, data = request("GET", "/api/chat/history", headers=ip2_headers)
        print(f"[TEST 6] /api/chat/history (IP2 empty) -> HTTP {status}: message_count={data['message_count']}")
        assert status == 200
        assert data["message_count"] == 0 # IP 2 has separate empty history!

        # Send chat from IP 2 with specific agent "mistral:latest"
        status, data = request("POST", "/api/chat", {"message": "Hello from IP2!", "agent": "mistral:latest"}, ip2_headers)
        print(f"[TEST 7] /api/chat (IP2 specific agent) -> HTTP {status}: agent='{data['agent']}'")
        assert status == 200
        assert data["user_ip"] == "192.168.1.202"
        assert data["agent"] == "mistral:latest"
        assert len(data["chat_history"]) == 2

        # Verify IP 1's history is still intact with 4 messages
        status, data = request("GET", "/api/chat/history", headers=ip1_headers)
        assert data["message_count"] == 4

        # Test 8: Inspect session metadata via /api/session
        status, data = request("GET", "/api/session", headers=ip1_headers)
        print(f"[TEST 8] /api/session (IP1) -> HTTP {status}: {data['session']}")
        assert status == 200
        assert data["session"]["user_ip"] == "192.168.1.101"
        assert data["session"]["message_count"] == 4

        # Test 9: Clear chat history for IP 1
        status, data = request("DELETE", "/api/chat/history", headers=ip1_headers)
        print(f"[TEST 9] DELETE /api/chat/history (IP1) -> HTTP {status}: {data['message']}")
        assert status == 200

        # Verify IP 1 history is now empty
        status, data = request("GET", "/api/chat/history", headers=ip1_headers)
        assert data["message_count"] == 0

        # Verify IP 2 history is still intact!
        status, data = request("GET", "/api/chat/history", headers=ip2_headers)
        assert data["message_count"] == 2

        # Test 10: Verify Database endpoints are removed (404)
        try:
            req = urllib.request.Request("http://127.0.0.1:8085/api/db/status")
            urllib.request.urlopen(req)
            assert False, "Should have 404'd"
        except urllib.error.HTTPError as e:
            print(f"[TEST 10] /api/db/status -> HTTP {e.code} (Correctly removed!)")
            assert e.code == 404

        # Test 11: Set session configuration via POST /api/session
        status, data = request("POST", "/api/session", {"preferred_agent": "mistral:latest", "persona": "coder"}, ip1_headers)
        print(f"[TEST 11] POST /api/session -> HTTP {status}: {data}")
        assert status == 200
        assert data["session"]["preferred_agent"] == "mistral:latest"
        assert data["session"]["persona"] == "coder"

        # Test 12: Embeddings vector generation via POST /api/embed
        status, data = request("POST", "/api/embed", {"input": ["Hello world", "Embed this test sentence"]})
        print(f"[TEST 12] POST /api/embed -> HTTP {status}: {len(data.get('embeddings', []))} embeddings generated")
        assert status == 200
        assert "embeddings" in data
        assert len(data["embeddings"]) == 2
        assert len(data["embeddings"][0]) == 3

        # Test 13: Export chat history (Markdown & JSON)
        # IP 2 has 2 messages
        status, md_data = request("GET", "/api/chat/export?format=markdown", headers=ip2_headers)
        print(f"[TEST 13a] GET /api/chat/export?format=markdown (IP2) -> HTTP {status}: length={len(md_data)}")
        assert status == 200
        assert "# Chat Session Export" in md_data
        assert "192.168.1.202" in md_data

        status, json_data = request("GET", "/api/chat/export?format=json", headers=ip2_headers)
        print(f"[TEST 13b] GET /api/chat/export?format=json (IP2) -> HTTP {status}: messages={len(json_data.get('messages', []))}")
        assert status == 200
        assert json_data["user_ip"] == "192.168.1.202"
        assert len(json_data["messages"]) == 2

        # Test 14: Import chat history into IP 1 via POST /api/chat/import
        import_payload = {
            "messages": [
                {"role": "user", "content": "Imported Question 1"},
                {"role": "assistant", "content": "Imported Answer 1", "model": "mistral:latest"}
            ]
        }
        status, data = request("POST", "/api/chat/import", import_payload, ip1_headers)
        print(f"[TEST 14] POST /api/chat/import (IP1) -> HTTP {status}: total_messages={data.get('total_messages')}")
        assert status == 200
        assert data["imported_count"] == 2
        assert data["total_messages"] == 2

        # Verify history now reflects imported messages
        status, data = request("GET", "/api/chat/history", headers=ip1_headers)
        assert status == 200
        assert data["message_count"] == 2
        assert data["messages"][0]["content"] == "Imported Question 1"

        # Test 15: Single-page Web Chat UI serving (GET / and GET /chat)
        status, html = request("GET", "/")
        print(f"[TEST 15a] GET / -> HTTP {status}: length={len(html)}")
        assert status == 200
        assert "Local Ollama AI Playground" in html

        status, html = request("GET", "/chat")
        print(f"[TEST 15b] GET /chat -> HTTP {status}: length={len(html)}")
        assert status == 200
        assert "Local Ollama AI Playground" in html

        print("\n==========================================")
        print("ALL TESTS PASSED SUCCESSFULLY!")
        print("==========================================")

    finally:
        server_proc.terminate()
        server_proc.wait()
        ollama_server.shutdown()
        ollama_server.server_close()

if __name__ == "__main__":
    run_test()
