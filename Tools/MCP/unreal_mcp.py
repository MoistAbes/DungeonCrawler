import sys
import os
import json
import urllib.request
import urllib.error
import argparse

MCP_URL = "http://127.0.0.1:8000/mcp"
PROTOCOL_VERSION = "2024-11-05"

class UnrealMcpClient:
    def __init__(self, url=MCP_URL):
        self.url = url
        self.session_id = None
        self.request_id = 0

    def _send_rpc(self, method, params=None):
        self.request_id += 1
        payload = {"jsonrpc": "2.0", "method": method}
        if not method.startswith("notifications/"):
            payload["id"] = self.request_id
        if params is not None:
            payload["params"] = params

        headers = {"Content-Type": "application/json"}
        if self.session_id:
            headers["Mcp-Session-Id"] = self.session_id
            headers["Mcp-Protocol-Version"] = PROTOCOL_VERSION

        req = urllib.request.Request(
            self.url,
            data=json.dumps(payload).encode("utf-8"),
            headers=headers
        )

        try:
            with urllib.request.urlopen(req, timeout=180) as resp:
                if "Mcp-Session-Id" in resp.headers:
                    self.session_id = resp.headers["Mcp-Session-Id"]
                body = resp.read().decode("utf-8")
                if not body.strip():
                    return None
                return json.loads(body)
        except urllib.error.HTTPError as e:
            err_body = e.read().decode("utf-8", errors="replace")
            raise RuntimeError(f"HTTP {e.code}: {err_body}")
        except urllib.error.URLError as e:
            raise ConnectionError(f"Nie mozna polaczyc sie z Unreal Editorem pod {self.url}. Czy edytor jest wlaczony? ({e})")

    def connect(self):
        """Inicjalizuje czysta sesje z edytorem Unreal Engine."""
        init_res = self._send_rpc("initialize", {
            "protocolVersion": PROTOCOL_VERSION,
            "capabilities": {},
            "clientInfo": {"name": "dungeon-crawler-mcp-bridge", "version": "1.0"}
        })
        self._send_rpc("notifications/initialized")
        return self.session_id

    def call_tool(self, tool_name, toolset_name=None, arguments=None):
        """Wywoluje narzedzie MCP bezposrednio w edytorze."""
        if not self.session_id:
            self.connect()

        params = {"name": "call_tool", "arguments": {
            "tool_name": tool_name,
            "arguments": arguments or {}
        }}
        if toolset_name:
            params["arguments"]["toolset_name"] = toolset_name

        resp = self._send_rpc("tools/call", params)
        if not resp:
            return None
        if "error" in resp:
            raise RuntimeError(f"MCP RPC Error: {resp['error']}")
        
        result = resp.get("result", {})
        if result.get("isError"):
            content = result.get("content", [])
            err_msg = content[0].get("text", "") if content else "Nieznany blad"
            raise RuntimeError(f"Tool execution error: {err_msg}")
        
        content = result.get("content", [])
        if content and "text" in content[0]:
            try:
                return json.loads(content[0]["text"])
            except Exception:
                return content[0]["text"]
        return result

    def get_current_level(self):
        res = self.call_tool(
            tool_name="get_current_level",
            toolset_name="editor_toolset.toolsets.scene.SceneTools"
        )
        if isinstance(res, dict) and "returnValue" in res:
            return res["returnValue"]
        return res

    def save_level(self, level_path=None):
        if not level_path:
            level_path = self.get_current_level()
        if not level_path:
            raise RuntimeError("Brak aktywnego poziomu do zapisania.")
        return self.call_tool(
            tool_name="save_assets",
            toolset_name="editor_toolset.toolsets.asset.AssetTools",
            arguments={"asset_paths": [level_path]}
        )

    def execute_script(self, script_code):
        """Uruchamia skrypt Pythona w srodowisku ProgrammaticToolset w edytorze."""
        res = self.call_tool(
            tool_name="execute_tool_script",
            toolset_name="editor_toolset.toolsets.programmatic.ProgrammaticToolset",
            arguments={"script": script_code}
        )
        if isinstance(res, dict) and "returnValue" in res:
            try:
                return json.loads(res["returnValue"])
            except Exception:
                return res["returnValue"]
        return res

def main():
    parser = argparse.ArgumentParser(description="Unreal Engine MCP CLI Bridge")
    subparsers = parser.add_subparsers(dest="command")

    # status
    subparsers.add_parser("status", help="Sprawdza polaczenie z edytorem i zwraca aktywna mape")

    # get-level
    subparsers.add_parser("get-level", help="Zwraca sciezke aktualnie otwartej mapy")

    # save-level
    save_parser = subparsers.add_parser("save-level", help="Zapisuje aktywna lub podana mape na dysk")
    save_parser.add_argument("--path", help="Sciezka assetu mapy (domyslnie aktywna mapa)", default=None)

    # run-script
    script_parser = subparsers.add_parser("run-script", help="Wywoluje skrypt Pythona z pliku przez ProgrammaticToolset")
    script_parser.add_argument("script_file", help="Sciezka do pliku .py z funkcja run()")

    args = parser.parse_args()

    client = UnrealMcpClient()

    if args.command == "status":
        try:
            client.connect()
            lvl = client.get_current_level()
            print(json.dumps({
                "status": "ONLINE",
                "session_id": client.session_id,
                "current_level": lvl
            }, indent=2))
        except Exception as e:
            print(json.dumps({"status": "OFFLINE", "error": str(e)}, indent=2))
            sys.exit(1)

    elif args.command == "get-level":
        client.connect()
        print(client.get_current_level())

    elif args.command == "save-level":
        client.connect()
        res = client.save_level(args.path)
        print(json.dumps(res, indent=2))

    elif args.command == "run-script":
        with open(args.script_file, "r", encoding="utf-8") as f:
            code = f.read()
        client.connect()
        res = client.execute_script(code)
        print(json.dumps(res, indent=2))

    else:
        parser.print_help()

if __name__ == "__main__":
    main()
