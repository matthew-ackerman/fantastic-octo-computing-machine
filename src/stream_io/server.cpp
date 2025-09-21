#include "stream_io/server.h"

#include "stream_io/tcp_protocol.h"
#include "engine/command.h"

#include <vector>
#include <string>
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <set>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>

namespace {

struct Client {
    int fd = -1;
    std::string buf;
    double next_turn_time = 0.0; // when <= sim_time, client has turn
    int team = -1;
};

static inline void send_line(int fd, const std::string& s) { (void)::send(fd, s.c_str(), s.size(), 0); }

static void remove_client(std::vector<Client>& cs, int fd) {
    cs.erase(std::remove_if(cs.begin(), cs.end(), [&](const Client& c){ return c.fd == fd; }), cs.end());
}

} // anonymous

void run_engine_server(int port, double min_time_step, const ServerCallbacks& cb)
{
    int srv = ::socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) { std::perror("socket"); return; }
    int yes = 1; setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) { std::perror("bind"); ::close(srv); return; }
    if (listen(srv, 8) < 0) { std::perror("listen"); ::close(srv); return; }
    int flags = fcntl(srv, F_GETFL, 0); if (flags >= 0) fcntl(srv, F_SETFL, flags | O_NONBLOCK);
    std::fprintf(stderr, "[engine] listening on 127.0.0.1:%d\n", port);

    std::vector<Client> clients;
    std::set<int> required_teams;
    if (cb.get_required_teams) { auto v = cb.get_required_teams(); required_teams.insert(v.begin(), v.end()); }
    bool initial_wait = !required_teams.empty();
    std::set<int> claimed_teams;
    double sim_time = 0.0;
    const double dt = (min_time_step > 0.0 ? min_time_step : (1.0/64.0));

    while (true) {
        // Build fd set
        fd_set rfds; FD_ZERO(&rfds);
        FD_SET(srv, &rfds);
        int maxfd = srv;
        for (const auto& c : clients) { FD_SET(c.fd, &rfds); if (c.fd > maxfd) maxfd = c.fd; }
        timeval tv{0, 1000 * 10}; // 10ms
        int rv = select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        if (rv < 0) { if (errno == EINTR) continue; std::perror("select"); break; }

        // Accept new clients
        if (FD_ISSET(srv, &rfds)) {
            sockaddr_in cli{}; socklen_t len = sizeof(cli);
            int fd = ::accept(srv, (sockaddr*)&cli, &len);
            if (fd >= 0) {
                int fl = fcntl(fd, F_GETFL, 0); if (fl >= 0) fcntl(fd, F_SETFL, fl | O_NONBLOCK);
                Client c; c.fd = fd; c.next_turn_time = sim_time; // has turn immediately when joining
                clients.push_back(std::move(c));
                std::fprintf(stderr, "[engine] client connected (fd=%d)\n", fd);
                // send initial snapshot
                if (cb.build_state_json) { std::string st = cb.build_state_json(false); send_line(fd, st); }
            }
        }

        // Read from clients
        for (size_t i = 0; i < clients.size(); ++i) {
            int fd = clients[i].fd;
            if (!FD_ISSET(fd, &rfds)) continue;
            char tmp[4096];
            while (true) {
                ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
                if (n > 0) { clients[i].buf.append(tmp, tmp + n); continue; }
                if (n == 0) { ::close(fd); clients[i].fd = -1; break; }
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                ::close(fd); clients[i].fd = -1; break;
            }
            if (clients[i].fd < 0) continue;
            size_t pos;
            while ((pos = clients[i].buf.find('\n')) != std::string::npos) {
                std::string line = clients[i].buf.substr(0, pos);
                clients[i].buf.erase(0, pos + 1);
                if (line.empty()) continue;
                tcp_protocol::ClientMsg msg; std::string perr;
                if (!tcp_protocol::parse_client_message(line, msg, &perr)) { send_line(fd, tcp_protocol::build_reply("error", perr.c_str())); continue; }
                using tcp_protocol::ClientMsgType;
                if (msg.type == ClientMsgType::Join) {
                    // Enforce unique team claim if provided
                    if (msg.team >= 0) {
                        bool taken = false; for (const auto& c2 : clients) if (c2.fd >= 0 && c2.team == msg.team) { taken = true; break; }
                        if (taken) { send_line(fd, tcp_protocol::build_reply("error", "team taken")); continue; }
                        clients[i].team = msg.team; claimed_teams.insert(msg.team);
                    }
                    // Compute defs hash match
                    const char* dh = nullptr; bool match=false; const bool* mp=nullptr; std::string defs_hash;
                    if (cb.get_defs_hash) { defs_hash = cb.get_defs_hash(); dh = defs_hash.c_str(); }
                    if (dh && !msg.defs_hash.empty()) { match = (defs_hash == msg.defs_hash); mp = &match; }
                    send_line(fd, tcp_protocol::build_joined_reply(dh?defs_hash:"", mp));
                } else if (msg.type == ClientMsgType::StateReq) {
                    bool all = false; if (!msg.scope.empty()) { all = (msg.scope == "all" || msg.scope == "ALL"); }
                    if (cb.build_state_json) { std::string sline = cb.build_state_json(all); send_line(fd, sline); }
                } else if (msg.type == ClientMsgType::Cmd) {
                    const auto& cc = msg.cmd; Command c; uint64_t uid = cc.uid;
                    if (cc.name == "THROTTLE") {
                        c.type = Command::Type::THROTTLE; c.uid = uid; c.a = cc.value; if (cb.find_ship_by_uid) c.ship = cb.find_ship_by_uid(uid); if (!c.ship) { send_line(fd, tcp_protocol::build_reply("error", "unknown uid")); continue; } if (cb.queue_command) cb.queue_command(c); send_line(fd, tcp_protocol::build_reply("ack", "THROTTLE"));
                    } else if (cc.name == "HEADING") {
                        c.type = Command::Type::HEADING; c.uid = uid; c.a = cc.theta; if (cb.find_ship_by_uid) c.ship = cb.find_ship_by_uid(uid); if (!c.ship) { send_line(fd, tcp_protocol::build_reply("error", "unknown uid")); continue; } if (cb.queue_command) cb.queue_command(c); send_line(fd, tcp_protocol::build_reply("ack", "HEADING"));
                    } else if (cc.name == "FIRE") {
                        c.type = Command::Type::FIRE; c.uid = uid; c.a = cc.theta; if (cb.find_ship_by_uid) { c.ship = cb.find_ship_by_uid(uid); if (c.ship) c.key = pick_projectile_key(*c.ship); } if (!c.ship) { send_line(fd, tcp_protocol::build_reply("error", "unknown uid")); continue; } if (cb.queue_command) cb.queue_command(c); send_line(fd, tcp_protocol::build_reply("ack", "FIRE"));
                    } else {
                        send_line(fd, tcp_protocol::build_reply("error", "unknown cmd"));
                    }
                } else if (msg.type == ClientMsgType::EndTurn) {
                    if (cb.apply_queued_commands) cb.apply_queued_commands();
                    if (cb.end_of_turn_cleanup) cb.end_of_turn_cleanup();
                    if (cb.rebuild_uid_map) cb.rebuild_uid_map();
                    // Schedule client's next turn
                    clients[i].next_turn_time = (msg.wait > 0.0 ? sim_time + msg.wait : sim_time);
                } else {
                    send_line(fd, tcp_protocol::build_reply("error", "unknown type"));
                }
            }
        }

        // Drop disconnected clients
        clients.erase(std::remove_if(clients.begin(), clients.end(), [](const Client& c){ return c.fd < 0; }), clients.end());

        // Determine if any client has a turn due
        bool any_due = false;
        for (const auto& c : clients) if (c.fd >= 0 && c.next_turn_time <= sim_time + 1e-12) { any_due = true; break; }

        // Initial wait until all required teams are claimed
        if (initial_wait) {
            bool all_claimed = true; for (int t : required_teams) if (!claimed_teams.count(t)) { all_claimed = false; break; }
            if (!all_claimed) { usleep(1000); continue; }
            initial_wait = false;
        }

        if (!any_due) {
            // Step simulation and broadcast
            if (cb.step_world_dt) cb.step_world_dt(dt);
            if (cb.rebuild_uid_map) cb.rebuild_uid_map();
            sim_time += dt;
            if (cb.build_state_json) {
                std::string sline = cb.build_state_json(false);
                for (const auto& c : clients) if (c.fd >= 0) send_line(c.fd, sline);
            }
        } else {
            // Pause stepping to let due client(s) act
            usleep(1000);
        }
    }

    for (const auto& c : clients) if (c.fd >= 0) ::close(c.fd);
    ::close(srv);
}
