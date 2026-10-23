// server.cpp — OPTIONAL localhost front-end for the simulator.
//
// This is a separate executable. The core project (src/main.cpp) remains a
// terminal program with no web server; this file only exposes the same crs_core
// engine over HTTP on 127.0.0.1 so results can be viewed in a browser.
// No third-party libraries: plain BSD sockets / Winsock, single-threaded.
//
// Endpoints
//   GET  /                         web/index.html
//   GET  /api/workloads            bundled workload names
//   GET  /api/workload?name=X      workload file text + its CONFIG suggestions
//   POST /api/run?sched=&place=&queue=&aging=&starve=      body: workload text
//   POST /api/compare?queue=&aging=&starve=                body: workload text
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using socket_t = SOCKET;
#define CLOSESOCK closesocket
#else
#include <arpa/inet.h>
#include <csignal>
#include <sys/time.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
#define INVALID_SOCKET (-1)
#define CLOSESOCK close
#endif

#include "Replay.h"
#include "Simulator.h"
#include "Workload.h"

using namespace crs;

namespace {

const char* kWorkloads[] = {"normal", "cpu_bottleneck", "memory_bottleneck", "starvation"};
constexpr std::size_t kMaxBody = 1 << 20;  // 1 MB

std::string g_root;  // project root containing web/ and workloads/

// ---------------------------------------------------------------- JSON helpers

std::string esc(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out + "\"";
}

std::string num(double v) {
    std::ostringstream os;
    os.precision(6);
    os << v;
    return os.str();
}

std::string configJson(const SimulationConfig& c) {
    return "{\"scheduling\":" + esc(toString(c.scheduling)) + ",\"placement\":" + esc(toString(c.placement)) +
           ",\"queueMode\":" + esc(toString(c.queueMode)) + ",\"agingInterval\":" + std::to_string(c.agingInterval) +
           ",\"starvationThreshold\":" + std::to_string(c.starvationThreshold) + ",\"label\":" + esc(c.label()) + "}";
}

std::string metricsJson(const Metrics& m) {
    std::string starved = "[";
    for (std::size_t i = 0; i < m.starvedTasks.size(); ++i) starved += (i ? "," : "") + std::to_string(m.starvedTasks[i]);
    starved += "]";
    return "{\"totalTasks\":" + std::to_string(m.totalTasks) + ",\"completed\":" + std::to_string(m.completed) +
           ",\"rejected\":" + std::to_string(m.rejected) + ",\"unfinished\":" + std::to_string(m.unfinished) +
           ",\"endTick\":" + std::to_string(m.endTick) + ",\"avgWaiting\":" + num(m.avgWaiting) +
           ",\"avgTurnaround\":" + num(m.avgTurnaround) + ",\"maxWaiting\":" + std::to_string(m.maxWaiting) +
           ",\"throughput\":" + num(m.throughput) + ",\"cpuUtilization\":" + num(m.cpuUtilization) +
           ",\"ramUtilization\":" + num(m.ramUtilization) +
           ",\"starvationThreshold\":" + std::to_string(m.starvationThreshold) + ",\"starved\":" + starved + "}";
}

std::string resultJson(const SimulationResult& r) {
    std::ostringstream os;
    os << "{\"workload\":" << esc(r.workloadName) << ",\"config\":" << configJson(r.config)
       << ",\"hitTickLimit\":" << (r.hitTickLimit ? "true" : "false") << ",\"vms\":[";
    for (std::size_t i = 0; i < r.vms.size(); ++i) {
        os << (i ? "," : "") << "{\"id\":" << i + 1 << ",\"name\":" << esc(r.vms[i].name) << ",\"cpu\":" << r.vms[i].cpu
           << ",\"ram\":" << r.vms[i].ram << "}";
    }
    os << "],\"tasks\":[";
    for (std::size_t i = 0; i < r.tasks.size(); ++i) {
        const Task& t = r.tasks[i];
        os << (i ? "," : "") << "{\"id\":" << t.id << ",\"cpu\":" << t.cpu << ",\"ram\":" << t.ram
           << ",\"arrival\":" << t.arrival << ",\"duration\":" << t.duration << ",\"priority\":" << t.basePriority
           << ",\"status\":" << esc(toString(t.status)) << ",\"vmId\":" << t.vmId << ",\"start\":" << t.startTime
           << ",\"completion\":" << t.completionTime << ",\"rejection\":" << t.rejectionTime << "}";
    }
    os << "],\"events\":[";
    for (std::size_t i = 0; i < r.events.size(); ++i) {
        const SimulationEvent& e = r.events[i];
        os << (i ? "," : "") << "{\"tick\":" << e.tick << ",\"taskId\":" << e.taskId << ",\"type\":" << esc(toString(e.type))
           << ",\"vmId\":" << e.vmId << ",\"reason\":" << esc(e.reason) << "}";
    }
    os << "],\"utilization\":[";
    for (std::size_t i = 0; i < r.utilization.size(); ++i) {
        const UtilizationSample& s = r.utilization[i];
        os << (i ? "," : "") << "[" << s.tick << "," << s.usedCpu << "," << s.usedRam << "]";
    }
    os << "],\"metrics\":" << metricsJson(r.metrics) << "}";
    return os.str();
}

// ---------------------------------------------------------------- HTTP helpers

std::string urlDecode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '+') {
            out += ' ';
        } else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
                   std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

std::map<std::string, std::string> parseQuery(const std::string& q) {
    std::map<std::string, std::string> out;
    std::istringstream ss(q);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        std::size_t eq = pair.find('=');
        if (eq == std::string::npos) out[urlDecode(pair)] = "";
        else out[urlDecode(pair.substr(0, eq))] = urlDecode(pair.substr(eq + 1));
    }
    return out;
}

struct Response {
    int status = 200;
    std::string type = "application/json";
    std::string body;
};

Response jsonError(int status, const std::string& msg) { return {status, "application/json", "{\"error\":" + esc(msg) + "}"}; }

bool readFile(const std::string& path, std::string& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

// Build a config from the workload's CONFIG lines, then apply explicit query overrides.
SimulationConfig configFrom(const Workload& w, const std::map<std::string, std::string>& q) {
    SimulationConfig cfg;
    applyWorkloadConfig(w, cfg);
    auto it = q.find("sched");
    if (it != q.end() && !it->second.empty() && !parseSchedulingPolicy(it->second, cfg.scheduling))
        throw std::runtime_error("unknown scheduling policy '" + it->second + "'");
    it = q.find("place");
    if (it != q.end() && !it->second.empty() && !parsePlacementPolicy(it->second, cfg.placement))
        throw std::runtime_error("unknown placement policy '" + it->second + "'");
    it = q.find("queue");
    if (it != q.end() && !it->second.empty() && !parseQueueMode(it->second, cfg.queueMode))
        throw std::runtime_error("unknown queue mode '" + it->second + "'");
    it = q.find("aging");
    if (it != q.end() && !it->second.empty()) cfg.agingInterval = std::max(1, std::stoi(it->second));
    it = q.find("starve");
    if (it != q.end() && !it->second.empty()) cfg.starvationThreshold = std::max(0, std::stoi(it->second));
    cfg.maxTicks = 20000;  // keep browser requests bounded
    return cfg;
}

Workload workloadFromBody(const std::string& body, const std::map<std::string, std::string>& q) {
    auto it = q.find("name");
    std::string name = it != q.end() && !it->second.empty() ? it->second : "custom";
    std::istringstream in(body);
    Workload w = parseWorkload(in, name);
    if (w.tasks.size() > 5000) throw std::runtime_error("too many tasks for the web view (max 5000)");
    return w;
}

Response route(const std::string& method, const std::string& target, const std::string& body) {
    std::size_t qpos = target.find('?');
    std::string path = target.substr(0, qpos);
    auto q = parseQuery(qpos == std::string::npos ? "" : target.substr(qpos + 1));

    try {
        if (method == "GET" && (path == "/" || path == "/index.html")) {
            Response r;
            r.type = "text/html; charset=utf-8";
            if (!readFile(g_root + "/web/index.html", r.body)) return jsonError(500, "web/index.html not found");
            return r;
        }
        if (method == "GET" && path == "/api/workloads") {
            std::string out = "[";
            for (std::size_t i = 0; i < sizeof kWorkloads / sizeof *kWorkloads; ++i) out += (i ? "," : "") + esc(kWorkloads[i]);
            return {200, "application/json", out + "]"};
        }
        if (method == "GET" && path == "/api/workload") {
            std::string name = q["name"];
            // Only the bundled names are served, so the URL can't be used to read arbitrary files.
            bool known = std::any_of(std::begin(kWorkloads), std::end(kWorkloads), [&](const char* k) { return name == k; });
            if (!known) return jsonError(404, "unknown workload '" + name + "'");
            std::string text;
            if (!readFile(g_root + "/workloads/" + name + ".txt", text)) return jsonError(500, "cannot read workload file");
            std::istringstream in(text);
            Workload w = parseWorkload(in, name);
            std::string cfg = "{";
            bool first = true;
            for (const auto& kv : w.config) {
                cfg += (first ? "" : ",") + esc(kv.first) + ":" + esc(kv.second);
                first = false;
            }
            return {200, "application/json", "{\"name\":" + esc(name) + ",\"text\":" + esc(text) + ",\"config\":" + cfg + "}}"};
        }
        if (method == "POST" && path == "/api/run") {
            Workload w = workloadFromBody(body, q);
            SimulationResult r = Simulator(w, configFrom(w, q)).run();
            return {200, "application/json", resultJson(r)};
        }
        if (method == "POST" && path == "/api/compare") {
            Workload w = workloadFromBody(body, q);
            auto results = replay(w, configFrom(w, q), allPolicyCombos());
            std::string out = "{\"results\":[";
            for (std::size_t i = 0; i < results.size(); ++i) {
                out += (i ? "," : "") + std::string("{\"config\":") + configJson(results[i].config) +
                       ",\"metrics\":" + metricsJson(results[i].metrics) + "}";
            }
            return {200, "application/json", out + "]}"};
        }
        return jsonError(404, "not found: " + method + " " + path);
    } catch (const std::exception& e) {
        return jsonError(400, e.what());
    }
}

const char* statusText(int s) {
    switch (s) {
        case 200: return "OK";
        case 400: return "Bad Request";
        case 404: return "Not Found";
        case 413: return "Payload Too Large";
        default: return "Internal Server Error";
    }
}

void sendAll(socket_t s, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        int n = static_cast<int>(send(s, data.data() + sent, static_cast<int>(data.size() - sent), 0));
        if (n <= 0) return;
        sent += static_cast<std::size_t>(n);
    }
}

void handle(socket_t client) {
    std::string req;
    char buf[8192];
    std::size_t headerEnd = std::string::npos;
    while (headerEnd == std::string::npos) {
        int n = static_cast<int>(recv(client, buf, sizeof buf, 0));
        if (n <= 0) return;
        req.append(buf, static_cast<std::size_t>(n));
        headerEnd = req.find("\r\n\r\n");
        if (req.size() > 64 * 1024 && headerEnd == std::string::npos) return;
    }

    std::istringstream head(req.substr(0, headerEnd));
    std::string method, target, version, line;
    head >> method >> target >> version;
    std::getline(head, line);
    std::size_t contentLength = 0;
    while (std::getline(head, line)) {
        std::string lowerLine = line;
        std::transform(lowerLine.begin(), lowerLine.end(), lowerLine.begin(), [](unsigned char c) { return std::tolower(c); });
        if (lowerLine.rfind("content-length:", 0) == 0) contentLength = std::strtoul(line.c_str() + 15, nullptr, 10);
    }

    Response res;
    if (contentLength > kMaxBody) {
        res = jsonError(413, "request body too large");
    } else {
        std::string body = req.substr(headerEnd + 4);
        while (body.size() < contentLength) {
            int n = static_cast<int>(recv(client, buf, sizeof buf, 0));
            if (n <= 0) return;
            body.append(buf, static_cast<std::size_t>(n));
        }
        body.resize(contentLength);
        res = route(method, target, body);
        std::cout << method << " " << target.substr(0, target.find('?')) << " -> " << res.status << std::endl;
    }

    std::ostringstream out;
    out << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\n"
        << "Content-Type: " << res.type << "\r\n"
        << "Content-Length: " << res.body.size() << "\r\n"
        << "Cache-Control: no-store\r\n"
        << "Connection: close\r\n\r\n"
        << res.body;
    sendAll(client, out.str());
}

std::string findRoot() {
    for (const char* dir : {".", "..", "../.."}) {
        std::ifstream probe(std::string(dir) + "/web/index.html");
        if (probe) return dir;
    }
    return ".";
}

}  // namespace

int main(int argc, char** argv) {
    int port = 8080;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--port" && i + 1 < argc) {
            port = std::atoi(argv[++i]);
        } else {
            std::cout << "Usage: CloudResourceSimulatorWeb [--port N]   (serves http://127.0.0.1:N)\n";
            return a == "--help" ? 0 : 1;
        }
    }
    g_root = findRoot();
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);  // a closed browser tab must not kill the server
#endif

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif

    socket_t server = socket(AF_INET, SOCK_STREAM, 0);
    if (server == INVALID_SOCKET) {
        std::cerr << "cannot create socket\n";
        return 1;
    }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&yes), sizeof yes);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<unsigned short>(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  // 127.0.0.1 only: not reachable from other machines

    if (bind(server, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || listen(server, 16) != 0) {
        std::cerr << "cannot listen on 127.0.0.1:" << port << " (port in use? try --port 8081)\n";
        CLOSESOCK(server);
        return 1;
    }

    std::cout << "Cloud Resource Allocation Simulator (web view)\n"
              << "Open http://127.0.0.1:" << port << " in your browser. Press Ctrl+C to stop.\n";

    while (true) {
        socket_t client = accept(server, nullptr, nullptr);
        if (client == INVALID_SOCKET) continue;
        // Don't let a silent client block the single-threaded server forever.
#ifdef _WIN32
        DWORD timeoutMs = 5000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof timeoutMs);
#else
        timeval tv{5, 0};
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
#endif
        handle(client);
        CLOSESOCK(client);
    }
}
