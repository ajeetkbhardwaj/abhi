// Minimal OpenAI-compatible streaming endpoint used to verify the Karta agent loop
// without a real API key. Scenario is picked from the user's prompt.
import http from "node:http";
import fs from "node:fs";

const port = Number(process.argv[2] || 8123);

function sse(res, payload) {
  res.write(`data: ${JSON.stringify(payload)}\n\n`);
}

const server = http.createServer((req, res) => {
  if (req.method !== "POST" || !req.url.endsWith("/chat/completions")) {
    res.writeHead(404).end();
    return;
  }
  let body = "";
  req.on("data", (chunk) => (body += chunk));
  req.on("end", () => {
    const request = JSON.parse(body);
    if (process.env.MOCK_LOG) {
      fs.appendFileSync(
        process.env.MOCK_LOG,
        JSON.stringify({ roles: request.messages.map((m) => m.role), lastKeys: Object.keys(request.messages.at(-1)) }) + "\n",
      );
    }
    const last = request.messages[request.messages.length - 1];
    const userText = [...request.messages]
      .reverse()
      .find((message) => message.role === "user")?.content || "";

    res.writeHead(200, {
      "Content-Type": "text/event-stream",
      "Cache-Control": "no-cache",
      Connection: "keep-alive",
    });

    const emitToolCall = (id, name, args) => {
      sse(res, { choices: [{ index: 0, delta: { role: "assistant" } }] });
      sse(res, {
        choices: [
          {
            index: 0,
            delta: {
              tool_calls: [{ index: 0, id, type: "function", function: { name, arguments: "" } }],
            },
          },
        ],
      });
      const half = Math.floor(args.length / 2);
      sse(res, {
        choices: [
          { index: 0, delta: { tool_calls: [{ index: 0, function: { arguments: args.slice(0, half) } }] } },
        ],
      });
      sse(res, {
        choices: [
          { index: 0, delta: { tool_calls: [{ index: 0, function: { arguments: args.slice(half) } }] } },
        ],
      });
      sse(res, { choices: [{ index: 0, delta: {}, finish_reason: "tool_calls" }] });
      res.write("data: [DONE]\n\n");
      res.end();
    };

    if (userText.includes("MULTI") && last.role !== "tool") {
      const calls = [
        ["call_write", "write", { path: "notes.txt", content: "created by karta\n" }],
        ["call_edit", "edit", { path: "sample.txt", edits: [{ oldText: "alpha", newText: "ALPHA" }] }],
        ["call_find", "find", { pattern: "*.txt", path: "." }],
        ["call_ls", "ls", { path: "." }],
      ];
      sse(res, { choices: [{ index: 0, delta: { role: "assistant" } }] });
      for (let i = 0; i < calls.length; i++) {
        const [id, name, args] = calls[i];
        const json = JSON.stringify(args);
        const half = Math.floor(json.length / 2);
        sse(res, {
          choices: [
            {
              index: 0,
              delta: {
                tool_calls: [
                  { index: i, id, type: "function", function: { name, arguments: json.slice(0, half) } },
                ],
              },
            },
          ],
        });
        sse(res, {
          choices: [
            { index: 0, delta: { tool_calls: [{ index: i, function: { arguments: json.slice(half) } }] } },
          ],
        });
      }
      sse(res, { choices: [{ index: 0, delta: {}, finish_reason: "tool_calls" }] });
      res.write("data: [DONE]\n\n");
      res.end();
      return;
    }

    if (last.role === "tool") {
      sse(res, { choices: [{ index: 0, delta: { role: "assistant", content: "Tool said: " } }] });
      sse(res, { choices: [{ index: 0, delta: { content: String(last.content).trim() } }] });
      sse(res, { choices: [{ index: 0, delta: {}, finish_reason: "stop" }] });
      sse(res, { choices: [], usage: { prompt_tokens: 42, completion_tokens: 7 } });
      res.write("data: [DONE]\n\n");
      res.end();
      return;
    }

    if (userText.includes("READ")) {
      emitToolCall("call_read", "read", JSON.stringify({ path: "sample.txt" }));
    } else if (userText.includes("GREP")) {
      emitToolCall("call_grep", "grep", JSON.stringify({ pattern: "needle", path: "." }));
    } else {
      emitToolCall(
        "call_bash",
        "bash",
        JSON.stringify({ command: "printf 'hello-from-karta\\n'" }),
      );
    }
  });
});

server.listen(port, "127.0.0.1", () => {
  console.log(`mock provider listening on ${port}`);
});
