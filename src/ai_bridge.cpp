    #include "AI_Bridge.h"

    #include <atomic>
    #include <chrono>
    #include <condition_variable>
    #include <deque>
    #include <mutex>
    #include <thread>
    #include <string>
    #include <curl/curl.h>


    class ai_bridge::impl
    {
        public:
            explicit impl( const options &opts ) : opts( opts ), stop( false ) {}

            options opts;

            struct pending_item {
                request_id id = 0;
                request req;
            };

            struct completed_item {
                completion done;
                completion_cb_t cb;
            };

            std::mutex mtx;
            std::condition_variable cv;
            std::deque<pending_item> pending;
            std::deque<completed_item> completed;
            std::unordered_map<request_id, completion_cb_t> callbacks;
            std::unordered_map<request_id, bool> cancelled;

            std::atomic<bool> stop;
            std::thread worker;
            request_id next_id = 1;

            auto start() -> void
            {
                worker = std::thread( [this]() {
                    worker_loop();
                } );
            }

            auto shutdown() -> void
            {
                stop.store( true );
                cv.notify_all();
                if( worker.joinable() ) {
                    worker.join();
                }
            }

            auto worker_loop() -> void
            {
                while( !stop.load() ) {
                    pending_item item;
                    {
                        std::unique_lock<std::mutex> lock( mtx );
                        cv.wait( lock, [&]() {
                            return stop.load() || !pending.empty();
                        } );
                        if( stop.load() ) {
                            return;
                        }
                        item = pending.front();
                        pending.pop_front();
                    }

                    auto raw = std::string{};
                    auto err = error_info{};
                    auto ok = false;

                    {
                        std::lock_guard<std::mutex> lock( mtx );
                        const auto it = cancelled.find( item.id );
                        if( it != cancelled.end() && it->second ) {
                            auto c = completion{};
                            c.id = item.id;
                            c.raw = "";
                            c.result.ok = false;
                            c.result.error.code = error_code::cancelled;
                            c.result.error.message = "cancelled";
                            queue_completion( std::move( c ) );
                            continue;
                        }
                    }

                    if( opts.transport ) {
                        ok = opts.transport( item.req, raw, err );
                    } else {
                        ok = false;
                        err.code = error_code::transport_error;
                        err.message = "no transport configured";
                    }

                    auto c = completion{};
                    c.id = item.id;
                    c.raw = raw;
                    if( ok ) {
                        c.result = ai_bridge::parse_response( raw, opts.ignore_invalid_commands );
                        if( !c.result.ok && c.result.error.code == error_code::none ) {
                            c.result.error.code = error_code::parse_error;
                        }
                    } else {
                        c.result.ok = false;
                        c.result.error = err;
                        if( c.result.error.code == error_code::none ) {
                            c.result.error.code = error_code::transport_error;
                        }
                    }

                    queue_completion( std::move( c ) );
                }
            }

            auto queue_completion( completion c ) -> void
            {
                auto cb = completion_cb_t{};
                {
                    std::lock_guard<std::mutex> lock( mtx );
                    const auto it = callbacks.find( c.id );
                    if( it != callbacks.end() ) {
                        cb = it->second;
                        callbacks.erase( it );
                    }
                    auto item = completed_item{};
                    item.done = std::move( c );
                    item.cb = std::move( cb );
                    completed.push_back( std::move( item ) );
                }
            }
    };

    ai_bridge::ai_bridge( const options &opts ) : pimpl( std::make_unique<impl>( opts ) )
    {
        pimpl->start();
    }

    ai_bridge::~ai_bridge()
    {
        pimpl->shutdown();
    }

    auto ai_bridge::enqueue( request req, completion_cb_t on_complete ) -> request_id
    {
        std::lock_guard<std::mutex> lock( pimpl->mtx );
        const auto id = pimpl->next_id++;
        pimpl->callbacks.emplace( id, std::move( on_complete ) );
        auto item = impl::pending_item{};
        item.id = id;
        item.req = std::move( req );
        pimpl->pending.push_back( std::move( item ) );
        pimpl->cv.notify_one();
        return id;
    }

    auto ai_bridge::cancel( request_id id ) -> void
    {
        std::lock_guard<std::mutex> lock( pimpl->mtx );
        pimpl->cancelled[id] = true;
    }

    auto ai_bridge::pump_callbacks( std::chrono::milliseconds budget ) -> std::size_t
    {
        const auto deadline = std::chrono::steady_clock::now() + budget;
        auto ran = std::size_t{ 0 };

        while( std::chrono::steady_clock::now() < deadline ) {
            auto item = impl::completed_item{};
            {
                std::lock_guard<std::mutex> lock( pimpl->mtx );
                if( pimpl->completed.empty() ) {
                    break;
                }
                item = std::move( pimpl->completed.front() );
                pimpl->completed.pop_front();
            }

            if( item.cb ) {
                item.cb( item.done );
                ran++;
            }
        }

        return ran;
    }

    auto ai_bridge::parse_response( const std::string &raw, const bool ignore_invalid_commands ) -> parse_result
    {
        auto out = parse_result{};
        if( raw.empty() ) {
            out.ok = false;
            out.error.code = error_code::invalid_response;
            out.error.message = "empty response";
            return out;
        }

        const auto first = raw.find_first_not_of( " \t\r\n" );
        if( first == std::string::npos || raw[first] != '{' ) {
            out.ok = true;
            out.response.text = raw;
            return out;
        }

        auto cursor = first;

        const auto skip_ws = [&]( ) {
            while( cursor < raw.size() ) {
                const auto ch = raw[cursor];
                if( ch != ' ' && ch != '\t' && ch != '\r' && ch != '\n' ) {
                    break;
                }
                cursor++;
            }
        };

        const auto parse_string = [&]( std::string &dst ) -> bool {
            skip_ws();
            if( cursor >= raw.size() || raw[cursor] != '"' ) {
                return false;
            }
            cursor++; 
            dst.clear();
            while( cursor < raw.size() ) {
                const auto ch = raw[cursor++];
                if( ch == '"' ) return true;
                if( ch == '\\' && cursor < raw.size() ) {
                    const auto esc = raw[cursor++];
                    switch( esc ) {
                        case '"': case '\\': case '/': dst.push_back( esc ); break;
                        case 'b': dst.push_back( '\b' ); break;
                        case 'f': dst.push_back( '\f' ); break;
                        case 'n': dst.push_back( '\n' ); break;
                        case 'r': dst.push_back( '\r' ); break;
                        case 't': dst.push_back( '\t' ); break;
                        default: dst.push_back( esc ); break;
                    }
                    continue;
                }
                dst.push_back( ch );
            }
            return false;
        };

        const auto skip_value = [&]( ) -> bool {
            skip_ws();
            if( cursor >= raw.size() ) return false;
            const auto ch = raw[cursor];
            if( ch == '"' ) {
                auto tmp = std::string{};
                return parse_string( tmp );
            }
            if( ch == '{' ) {
                auto depth = 0;
                do {
                    const auto c = raw[cursor++];
                    if( c == '"' ) {
                        cursor--;
                        auto tmp = std::string{};
                        if( !parse_string( tmp ) ) return false;
                        continue;
                    }
                    if( c == '{' ) depth++;
                    else if( c == '}' ) depth--;
                } while( cursor < raw.size() && depth > 0 );
                return depth == 0;
            }
            if( ch == '[' ) {
                auto depth = 0;
                do {
                    const auto c = raw[cursor++];
                    if( c == '"' ) {
                        cursor--;
                        auto tmp = std::string{};
                        if( !parse_string( tmp ) ) return false;
                        continue;
                    }
                    if( c == '[' ) depth++;
                    else if( c == ']' ) depth--;
                } while( cursor < raw.size() && depth > 0 );
                return depth == 0;
            }
            while( cursor < raw.size() ) {
                const auto c = raw[cursor];
                if( c == ',' || c == '}' || c == ']' || c == ' ' || c == '\t' || c == '\r' || c == '\n' ) break;
                cursor++;
            }
            return true;
        };

        auto key = std::string{};
        auto value = std::string{};

        skip_ws();
        if( cursor >= raw.size() || raw[cursor] != '{' ) {
            out.ok = false;
            out.error.code = error_code::parse_error;
            out.error.message = "expected object";
            return out;
        }
        cursor++; 

        while( true ) {
            skip_ws();
            if( cursor >= raw.size() ) {
                out.ok = false;
                out.error.code = error_code::parse_error;
                out.error.message = "unexpected end of input";
                return out;
            }
            if( raw[cursor] == '}' ) {
                cursor++;
                break;
            }
            if( !parse_string( key ) ) {
                out.ok = false;
                out.error.code = error_code::parse_error;
                out.error.message = "expected string key";
                return out;
            }
            skip_ws();
            if( cursor >= raw.size() || raw[cursor] != ':' ) {
                out.ok = false;
                out.error.code = error_code::parse_error;
                out.error.message = "expected ':'";
                return out;
            }
            cursor++; 

            if( key == "text" ) {
                if( parse_string( value ) ) out.response.text = value;
                else if( !skip_value() ) { out.ok = false; out.error.code = error_code::parse_error; out.error.message = "invalid 'text' value"; return out; }
            } else if( key == "action" ) {
                if( parse_string( value ) ) {
                    auto cmd = command{};
                    cmd.action = value;
                    out.response.commands.push_back( std::move( cmd ) );
                } else if( !skip_value() ) { out.ok = false; out.error.code = error_code::parse_error; out.error.message = "invalid 'action' value"; return out; }
            } else if( key == "target" ) {
                if( parse_string( value ) ) {
                    if( !out.response.commands.empty() ) out.response.commands.back().target = value;
                } else if( !skip_value() ) { out.ok = false; out.error.code = error_code::parse_error; out.error.message = "invalid 'target' value"; return out; }
            } else {
                if( !skip_value() ) { out.ok = false; out.error.code = error_code::parse_error; out.error.message = "invalid value"; return out; }
            }

            skip_ws();
            if( cursor < raw.size() && raw[cursor] == ',' ) {
                cursor++;
                continue;
            }
            if( cursor < raw.size() && raw[cursor] == '}' ) {
                cursor++;
                break;
            }
        }

        if( out.response.text.empty() && out.response.commands.empty() ) {
            out.ok = false;
            out.error.code = error_code::invalid_response;
            out.error.message = "unrecognized JSON response schema";
            return out;
        }

        if( !ignore_invalid_commands ) {
            for( const command &cmd : out.response.commands ) {
                if( cmd.action.empty() ) {
                    out.ok = false;
                    out.error.code = error_code::parse_error;
                    out.error.message = "command missing action";
                    return out;
                }
            }
        }

        out.ok = true;
        return out;
    }
    // --- 아래부터 libcurl 통신 모듈 (TCP Keep-Alive 적용) ---

    static auto escape_json(const std::string& s) -> std::string {
        std::string out;
        for (char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\b') out += "\\b";
            else if (c == '\f') out += "\\f";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else if (c == '\t') out += "\\t";
            else out += c;
        }
        return out;
    }

    static auto write_to_string_cb(char *ptr, size_t size, size_t nmemb, void *userdata) -> size_t {
        if (userdata == nullptr) return 0;
        auto &out = *static_cast<std::string *>(userdata);
        out.append(ptr, size * nmemb);
        return size * nmemb;
    }

    struct curl_global_guard {
        curl_global_guard() { curl_global_init(CURL_GLOBAL_DEFAULT); }
        ~curl_global_guard() { curl_global_cleanup(); }
    };

    auto curl_transport_localhost_keepalive( const ai_bridge::request &req,
                                            const ai_bridge::options &opts,
                                            std::string &raw_out,
                                            ai_bridge::error_info &err_out ) -> bool
    {
        static curl_global_guard guard;
        raw_out.clear();
        err_out.code = ai_bridge::error_code::none;
        err_out.message.clear();

        CURL *curl = curl_easy_init();
        if( curl == nullptr ) {
            err_out.code = ai_bridge::error_code::transport_error;
            err_out.message = "curl_easy_init failed";
            return false;
        }

        struct curl_slist *headers = nullptr;
        headers = curl_slist_append( headers, "Content-Type: application/json" );
        headers = curl_slist_append( headers, "Accept: application/json" );

        // 종속성 에러를 막기 위한 수동 JSON 페이로드 빌더
        std::string payload = "{";
        payload += "\"model\": \"" + (opts.model.empty() ? "mistral-22b" : escape_json(opts.model)) + "\",";
        payload += "\"messages\": [";
        
        bool first_msg = true;
        if (!req.system.empty()) {
            payload += "{\"role\": \"system\", \"content\": \"" + escape_json(req.system) + "\"}";
            first_msg = false;
        }

        std::string context = "";
        if (!req.ctx.game_state_summary.empty()) context += "GAME_STATE:\\n" + escape_json(req.ctx.game_state_summary) + "\\n";
        if (!req.ctx.proximity_npcs_summary.empty()) context += "PROXIMITY_NPCS:\\n" + escape_json(req.ctx.proximity_npcs_summary) + "\\n";
        if (!req.ctx.player_status_summary.empty()) context += "PLAYER_STATUS:\\n" + escape_json(req.ctx.player_status_summary) + "\\n";
        if (!req.ctx.conversation_summary.empty()) context += "CONVERSATION:\\n" + escape_json(req.ctx.conversation_summary) + "\\n";

        if (!context.empty()) {
            if (!first_msg) payload += ",";
            payload += "{\"role\": \"user\", \"content\": \"" + context + "\"}";
            first_msg = false;
        }

        if (!first_msg) payload += ",";
        payload += "{\"role\": \"user\", \"content\": \"" + escape_json(req.user) + "\"}";
        payload += "]"; 

        if (req.max_tokens >= 0) payload += ",\"max_tokens\": " + std::to_string(req.max_tokens);
        if (req.temperature >= 0.0f) payload += ",\"temperature\": " + std::to_string(req.temperature);
        payload += ",\"stream\": false}";

        curl_easy_setopt( curl, CURLOPT_URL, opts.endpoint.c_str() );
        curl_easy_setopt( curl, CURLOPT_HTTPHEADER, headers );
        curl_easy_setopt( curl, CURLOPT_POST, 1L );
        curl_easy_setopt( curl, CURLOPT_POSTFIELDS, payload.c_str() );
        curl_easy_setopt( curl, CURLOPT_POSTFIELDSIZE, static_cast<long>( payload.size() ) );

        // 핵심: 로컬 통신 속도를 극한으로 끌어올리는 Keep-Alive
        curl_easy_setopt( curl, CURLOPT_TCP_KEEPALIVE, 1L );
        curl_easy_setopt( curl, CURLOPT_TCP_KEEPIDLE, 15L );
        curl_easy_setopt( curl, CURLOPT_TCP_KEEPINTVL, 15L );

        curl_easy_setopt( curl, CURLOPT_CONNECTTIMEOUT_MS, 500L );
        curl_easy_setopt( curl, CURLOPT_TIMEOUT_MS, static_cast<long>( req.timeout.count() ) );
        curl_easy_setopt( curl, CURLOPT_WRITEFUNCTION, &write_to_string_cb );
        curl_easy_setopt( curl, CURLOPT_WRITEDATA, &raw_out );

        const CURLcode rc = curl_easy_perform( curl );
        long http_code = 0;
        curl_easy_getinfo( curl, CURLINFO_RESPONSE_CODE, &http_code );

        curl_slist_free_all( headers );
        curl_easy_cleanup( curl );

        if( rc != CURLE_OK ) {
            err_out.code = ai_bridge::error_code::transport_error;
            err_out.message = curl_easy_strerror( rc );
            return false;
        }
        if( http_code < 200 || http_code >= 300 ) {
            err_out.code = ( http_code == 429 ) ? ai_bridge::error_code::rate_limited : ai_bridge::error_code::transport_error;
            err_out.message = "HTTP " + std::to_string( http_code );
            return false;
        }
        auto msg_idx = raw_out.find("\"message\"");
        if (msg_idx != std::string::npos) {
            auto content_idx = raw_out.find("\"content\"", msg_idx);
            if (content_idx != std::string::npos) {
                auto start_quote = raw_out.find('"', content_idx + 9);
                if (start_quote != std::string::npos) {
                    start_quote++;
                    std::string extracted;
                    for (size_t i = start_quote; i < raw_out.size(); ++i) {
                        if (raw_out[i] == '\\' && i + 1 < raw_out.size()) {
                            char esc = raw_out[i+1];
                            if (esc == '"' || esc == '\\' || esc == '/') extracted += esc;
                            else if (esc == 'n') extracted += '\n';
                            else if (esc == 'r') extracted += '\r';
                            else if (esc == 't') extracted += '\t';
                            else if (esc == 'b') extracted += '\b';
                            else if (esc == 'f') extracted += '\f';
                            else extracted += esc;
                            i++;
                        } else if (raw_out[i] == '"') {
                            break;
                        } else {
                            extracted += raw_out[i];
                        }
                    }
                    raw_out = extracted; // 껍데기를 벗긴 순수 LLM 텍스트/JSON으로 교체!
                }
            }
        }

        return true;
    }