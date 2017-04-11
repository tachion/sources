#include "ets_sys.h"
#include "stdout.h"
#include "osapi.h"
#include "ip_addr.h"
#include "mem.h"
#include "base64.h"

#include "user_timer.h"
#include "user_webclient.h"
#include "user_websocket.h"
#include "user_config.h"
#include "user_events.h"
#include "user_json.h"

STAILQ_HEAD(webclient_requests, _webclient_request_) webclient_requests = STAILQ_HEAD_INITIALIZER(webclient_requests);

LOCAL uint16 reconnect_timer = 0;

const char ICACHE_FLASH_ATTR *webclient_method_str(request_method method) {
	switch (method) {
		case GET         : return "GET";
		case POST        : return "POST";
		case OPTIONS     : return "OPTIONS";
	}
	return "UNKNOWN";
}

LOCAL struct espconn ICACHE_FLASH_ATTR *webclient_new_connection(int port) {
	struct espconn *connection = (struct espconn *)os_zalloc(sizeof(struct espconn));
	connection->proto.tcp = (esp_tcp *)os_zalloc(sizeof(esp_tcp));
	
	connection->type = ESPCONN_TCP;
	connection->state = ESPCONN_NONE;
	
	connection->proto.tcp->remote_port = port;
	connection->proto.tcp->local_port = espconn_port();
	
	return connection;
}

LOCAL void ICACHE_FLASH_ATTR webclient_free_connection(struct espconn *connection, bool ssl) {
	websocket_connection_clear(connection);
	
	sint8 res;
#if SSL_ENABLE
	if(connection->proto.tcp->remote_port == WEBSERVER_SSL_PORT || ssl) {
		res = espconn_secure_delete(connection);
	} else {
		res = espconn_delete(connection);
	}
#else
	res = espconn_delete(connection);
#endif
	
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
	if (res != 0) {
		debug(
			"WEBCLIENT: Connection delete %d [%s]\n", 
			res, 
			connection_err_str(res)
		);
	} else {
		debug("WEBCLIENT: Connection deleted.\n");
	}
#endif
	
	if (connection->proto.tcp) os_free(connection->proto.tcp);
	os_free(connection);
}

LOCAL webclient_request ICACHE_FLASH_ATTR *webclient_request_find(char *host, int port, char *path) {
	webclient_request *request;
	
#if WEBCLIENT_DEBUG
#if WEBCLIENT_VERBOSE_OUTPUT
	uint8 count=0;
	STAILQ_FOREACH(request, &webclient_requests, entries) {
		count++;
	}
	debug("WEBCLIENT: Requests count %d\n", count);
#endif
#endif
	
	STAILQ_FOREACH(request, &webclient_requests, entries) {
		if (
			os_strcmp(request->host, host) == 0 &&
			os_strcmp(request->path, path) == 0 &&
			request->connection->proto.tcp->remote_port == port
		) {
			return request;
		}
	}
	
	return NULL;
}

const char ICACHE_FLASH_ATTR *webclient_get_status(char *host, int port, char *path) {
	webclient_request *request = webclient_request_find(host, port, path);
	
	if (request != NULL) {
		if (request->state != HTTP || is_websocket(request->connection)) {
			if (request->connection != NULL && request->connection->state != ESPCONN_CLOSE) {
				return CONNECTED;
			}
		}
		return DISCONNECTED;
	} else {
		return DISCONNECTED;
	}
}

LOCAL webclient_request ICACHE_FLASH_ATTR *webclient_request_port_find(int local_port) {
	webclient_request *request;
	
#if WEBCLIENT_DEBUG
#if WEBCLIENT_VERBOSE_OUTPUT
	uint8 count=0;
	STAILQ_FOREACH(request, &webclient_requests, entries) {
		count++;
	}
	debug("WEBCLIENT: Requests count %d\n", count);
#endif
#endif
	
	STAILQ_FOREACH(request, &webclient_requests, entries) {
		if (request->connection->proto.tcp->local_port == local_port) {
			return request;
		}
	}
	
	return NULL;
}

LOCAL char ICACHE_FLASH_ATTR *webclient_authorization(char *user, char *password) {
	char *authorization;
	
	if (user != NULL && password != NULL && os_strlen(user) + os_strlen(password) != 0) {
		authorization = (char *)os_zalloc(50 + os_strlen(user) + os_strlen(password) + 1);
		uint16 len = os_strlen(user) + os_strlen(password) + 2;
		
		char credentials[len];
		os_memset(credentials, '\0', sizeof(credentials));
		os_sprintf(credentials, "%s:%s", user, password);
		
		char b64Credentials[len*2];
		os_memset(b64Credentials, '\0', sizeof(b64Credentials));
		
		int b64len = base64_encode(credentials, os_strlen(credentials), b64Credentials, sizeof(b64Credentials));
		if (b64len > 0) {
			os_sprintf(authorization, "Authorization: Basic %s\r\n", b64Credentials);
		}
	} else {
		authorization = (char *)os_zalloc(1);
	}
	
	return authorization;
}

LOCAL void ICACHE_FLASH_ATTR webclient_new_element(char **a, char *b) {
	if (*a != NULL) {
		os_free(*a);
	}
	
	if (b != NULL) {
		*a = (char *)os_zalloc(os_strlen(b)+1);
		os_strcpy(*a, b, os_strlen(b));
	} else {
		*a = (char *)os_zalloc(1);
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_reset_timers(webclient_request *request) {
	if (request == NULL) {
		return;
	}
	
	if (request->retry_timer != 0) {
		clearTimeout(request->retry_timer);
		request->retry_timer = 0;
	}
	
	if (request->disconnect_timer != 0) {
		clearTimeout(request->disconnect_timer);
		request->disconnect_timer = 0;
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_renew_connection(webclient_request *request) {
	if (request->state != HTTP && request->connection->state == ESPCONN_CLOSE) {
		struct espconn *old_connection = request->connection;
			
		request->state = UPGRADE;
		request->connection = webclient_new_connection(old_connection->proto.tcp->remote_port);
		os_memcpy(request->connection->proto.tcp->remote_ip, old_connection->proto.tcp->remote_ip, sizeof(ip_addr_t));
		request->ip = (ip_addr_t *)&(request->connection->proto.tcp->remote_ip);
		
#if WEBCLIENT_DEBUG
		debug(
			"WEBCLIENT: Renew [%d.%d.%d.%d:%d:%d] -> [%d.%d.%d.%d:%d:%d]\n", 
			IP2STR(old_connection->proto.tcp->remote_ip), 
			old_connection->proto.tcp->local_port, 
			old_connection->proto.tcp->remote_port, 
			IP2STR(request->connection->proto.tcp->remote_ip), 
			request->connection->proto.tcp->local_port,
			request->connection->proto.tcp->remote_port
		);
#endif
		
		webclient_free_connection(old_connection, request->ssl);
	}
}

LOCAL webclient_request ICACHE_FLASH_ATTR *webclient_new_request(
	request_method method, 
	bool ssl, 
	char *user, 
	char *password, 
	char *host, 
	int port, 
	char *path, 
	char *headers, 
	char *content
) {
	webclient_request *request = webclient_request_find(host, port, path);
	
	if (
		request != NULL && 
		(request->state != HTTP || is_websocket(request->connection))
	) {
		if (request->ip->addr == 0) {
#if WEBCLIENT_DEBUG
			debug("WEBCLIENT: Host not resolved yet!\n");
#endif
			return NULL;
		}
		webclient_renew_connection(request);
		webclient_new_element(&request->content, content);
		request->retry = 0;
		return request;
	}
	
	request = (webclient_request *)os_zalloc(sizeof(webclient_request));
	request->method = method;
	request->ssl = ssl;
	request->state = HTTP;
	request->retry = 0;
	request->retry_timer = 0;
	request->disconnect_timer = 0;
	request->response_state = WEBCLIENT_RESP_STATE_INIT;
	
	webclient_new_element(&request->user, user);
	webclient_new_element(&request->password, password);
	request->authorization = webclient_authorization(user, password);
	
	webclient_new_element(&request->host, host);
	webclient_new_element(&request->path, path);
	webclient_new_element(&request->headers, headers);
	webclient_new_element(&request->content, content);
	
	request->connection = webclient_new_connection(port);
	request->ip = (ip_addr_t *)&(request->connection->proto.tcp->remote_ip);
	
#if WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: New request %s:%d%s [:%d]\n", 
		host, 
		port, 
		path, 
		request->connection->proto.tcp->local_port
	);
#endif
	STAILQ_INSERT_TAIL(&webclient_requests, request, entries);
	return request;
}

LOCAL void ICACHE_FLASH_ATTR webclient_free_request(webclient_request *request) {
#if WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: Free request %s:%d%s [:%d]\n", 
		request->host, 
		request->connection->proto.tcp->remote_port,
		request->path,
		request->connection->proto.tcp->local_port
	);
#endif
	STAILQ_REMOVE(&webclient_requests, request, _webclient_request_, entries);
	webclient_reset_timers(request);
	
	if (request->user) os_free(request->user);
	if (request->password) os_free(request->password);
	if (request->authorization) os_free(request->authorization);
	
	if (request->host) os_free(request->host);
	if (request->path) os_free(request->path);
	if (request->headers) os_free(request->headers);
	if (request->content) os_free(request->content);
	
	if (request->connection) webclient_free_connection(request->connection, request->ssl);
	os_free(request);
}

LOCAL void ICACHE_FLASH_ATTR webclient_raise_reconnect() {
	char status[WEBSERVER_MAX_VALUE];
	char data[WEBSERVER_MAX_VALUE];
	
	user_event_raise(
		NULL, 
		json_status(
			status, ESP8266, "WebSocket Reconnect", 
			json_sprintf(
				data,
				"\"FreeHeap\" : %d",
				system_get_free_heap_size()
			)
		)
	);
}

LOCAL void ICACHE_FLASH_ATTR webclient_error(webclient_request *request, struct espconn *connection) {
	if (request == NULL) {
		webclient_free_connection(connection, false);
		return;
	}
	
	if (
		request->state != HTTP && 
		connection->state == ESPCONN_CLOSE
	) {
		uint8 retry = request->retry;
		
		webclient_free_request(request);
		user_event_server_error();
		
		if (WEBCLIENT_RETRY_MAX == 0 || retry < WEBCLIENT_RETRY_MAX) {
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
			debug("WEBCLIENT: New request after [%d]\n", WEBCLIENT_RETRY_AFTER);
#endif
			clearTimeout(reconnect_timer);
			reconnect_timer = setTimeout(
				(os_timer_func_t *)webclient_raise_reconnect, 
				NULL, 
				WEBCLIENT_RETRY_AFTER
			);
		}
		return;
	}

	if (wifi_station_get_connect_status() == STATION_GOT_IP) {
		if (WEBCLIENT_RETRY_MAX == 0 || request->retry < WEBCLIENT_RETRY_MAX) {
			request->retry++;
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
			debug("WEBCLIENT: Retry [%d] after [%d] of [%d]\n", request->retry, WEBCLIENT_RETRY_AFTER, WEBCLIENT_RETRY_MAX);
#endif
			
			// Prevent previous retry attempt to be fired
			webclient_reset_timers(request);
			
			request->retry_timer = setTimeout(
				(os_timer_func_t *)webclient_execute, 
				request, 
				WEBCLIENT_RETRY_AFTER
			);
		} else {
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
			debug("WEBCLIENT: Retry limit reached [%d]\n", request->retry);
#endif
			webclient_free_request(request);
		}
	} else {
		webclient_free_request(request);
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_timeout(struct espconn *connection) {
	int8 ret_stat = 0;
	
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: Timeout [%d.%d.%d.%d:%d:%d]\n", 
		IP2STR(connection->proto.tcp->remote_ip), 
		connection->proto.tcp->local_port,
		connection->proto.tcp->remote_port
	);
#endif	
	
	webclient_request *request = webclient_request_port_find(connection->proto.tcp->local_port);
	if (request == NULL) {
		debug("WEBCLIENT: webclient_timeout() request not found\n");
		return;
	}
	
#if SSL_ENABLE
	if(connection->proto.tcp->remote_port == WEBSERVER_SSL_PORT || (request != NULL && request->ssl)) {
		ret_stat = espconn_secure_disconnect(connection);
	} else {
		ret_stat = espconn_disconnect(connection);
	}
#else
	ret_stat = espconn_disconnect(connection);
#endif
	
	if (ret_stat != 0) {
		webclient_error(request, connection);
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_reconnect(void *arg, sint8 err) {
	struct espconn *connection = arg;
	
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: Reconnect [%d.%d.%d.%d:%d:%d] %d [%s] [%s]\n", 
		IP2STR(connection->proto.tcp->remote_ip), 
		connection->proto.tcp->local_port, 
		connection->proto.tcp->remote_port, 
		err,
		connection_err_str(err),
		connection_state_str(connection->state)
	);
#endif
	
	webclient_request *request = webclient_request_port_find(connection->proto.tcp->local_port);
	if (request == NULL) {
		debug("WEBCLIENT: webclient_reconnect() request not found\n");
		return;
	}
	
	if (err != 0) {
		webclient_error(request, connection);
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_disconnect(void *arg) {
	struct espconn *connection = arg;
	
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: Disconnected [%d.%d.%d.%d:%d:%d] [%s]\n", 
		IP2STR(connection->proto.tcp->remote_ip), 
		connection->proto.tcp->local_port,
		connection->proto.tcp->remote_port,
		connection_state_str(connection->state)
	);
#endif
	
	webclient_request *request = webclient_request_port_find(connection->proto.tcp->local_port);
	if (request == NULL) {
		debug("WEBCLIENT: webclient_disconnect() request not found\n");
		return;
	}
	
	// after closing HTTP; and received data OK
	if (
		request != NULL &&
		request->state == HTTP &&
		connection->state == ESPCONN_CLOSE && 
		request->response_state == WEBCLIENT_RESP_STATE_OK
	) {
		webclient_free_request(request);
	} else {
		webclient_error(request, connection);
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_sent(void *arg) {
	struct espconn *connection = arg;
	
	user_event_server_ok();
	
	if (is_websocket(connection)) {
		websocket_sent(arg);
		return;
	}
	
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: Done [%d.%d.%d.%d:%d:%d].\n", 
		IP2STR(connection->proto.tcp->remote_ip), 
		connection->proto.tcp->local_port,
		connection->proto.tcp->remote_port
	);
#endif
	
	webclient_request *request = webclient_request_port_find(connection->proto.tcp->local_port);
	if (request == NULL) {
		debug("WEBCLIENT: webclient_sent() request not found");
		return;
	}
	
	request->response_state = WEBCLIENT_RESP_STATE_WAIT;
}

LOCAL void ICACHE_FLASH_ATTR webclient_recv(void *arg, char *pData, unsigned short length) {
	struct espconn *connection = arg;
	
	if (websocket_switched(connection, pData, length)) {
		return;
	}
	
#if WEBCLIENT_DEBUG
#if WEBCLIENT_VERBOSE_OUTPUT
	debug(
		"WEBCLIENT: Receive [%d.%d.%d.%d:%d:%d]\n", 
		IP2STR(connection->proto.tcp->remote_ip), 
		connection->proto.tcp->local_port,
		connection->proto.tcp->remote_port
	);
	debug("WEBCLIENT RESPONSE:\n%s\n\n", pData);
#endif
#endif
	
	webclient_request *request = webclient_request_port_find(connection->proto.tcp->local_port);
	if (request == NULL) {
		debug("WEBCLIENT: webclient_recv() request not found");
		return;
	}
	
	request->retry = 0;
	webclient_reset_timers(request);
	
	if (websocket_client_upgrade(connection, request->ssl, EVENTS_URL, pData, request->headers)) {
		request->state = WEBSOCKET;
		if (os_strlen(request->user) + os_strlen(request->password) != 0) {
			char auth[WEBSERVER_MAX_VALUE];
			os_sprintf(auth, "{\"User\" : \"%s\", \"Password\" : \"%s\"}", request->user, request->password);
			websocket_send_message(EVENTS_URL, auth, request->connection);
		}
		if (os_strlen(user_config_events_name()) + os_strlen(user_config_events_token()) != 0) {
			char ident[WEBSERVER_MAX_VALUE];
			os_sprintf(ident, "{\"Name\" : \"%s\", \"Token\" : \"%s\"}", user_config_events_name(), user_config_events_token());
			websocket_send_message(EVENTS_URL, ident, request->connection);
		}
		webclient_execute(request);
		return;
	}
	
	bool is_close_conn = false;
	
	// Check if header && not chunked
	if (request->response_state == WEBCLIENT_RESP_STATE_WAITCHUNK) {
		if ((char *)os_strstr(pData, "0\r\n") == pData) {
			request->response_state = WEBCLIENT_RESP_STATE_OK;
			is_close_conn = true;
		}
	} else {
		if (os_strstr(pData, "Transfer-Encoding: chunked") > 0) {
			request->response_state = WEBCLIENT_RESP_STATE_WAITCHUNK;
		} else {
			request->response_state = WEBCLIENT_RESP_STATE_OK;
			is_close_conn = true;
		}
	}
	
	// close if we are happy with data
	if (is_close_conn) {
		setTimeout(
#if SSL_ENABLE	
			connection->proto.tcp->remote_port == WEBSERVER_SSL_PORT || request->ssl ?
				(os_timer_func_t *)espconn_secure_disconnect
				:
#endif
				(os_timer_func_t *)espconn_disconnect
			, 
			arg,
			100
		);
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_connect(void *arg) {
	struct espconn *connection = arg;
	
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: Connected [%d.%d.%d.%d:%d:%d]\n", 
		IP2STR(connection->proto.tcp->remote_ip), 
		connection->proto.tcp->local_port,
		connection->proto.tcp->remote_port
	);
#endif
	
	webclient_request *request = webclient_request_port_find(connection->proto.tcp->local_port);
	if (request == NULL) {
		debug("WEBCLIENT: webclient_connect() request not found\n");
		return;
	}
	
	espconn_regist_sentcb(connection, webclient_sent);
	espconn_regist_recvcb(connection, webclient_recv);
	
	if (connection->state == ESPCONN_CONNECT) {
		char *content;
		if (request->method == POST && os_strlen(request->content) != 0) {
			content = (char *)os_zalloc(os_strlen(request->content)+WEBSERVER_MAX_VALUE);
			os_sprintf(
				content,
				"Content-Type: application/json\r\n"
				"Content-Length: %d\r\n"
				"\r\n"
				"%s",
				os_strlen(request->content),
				request->content
			);
		} else {
			content = (char *)os_zalloc(3);
			os_sprintf(content, "\r\n");
		}
		
		char *request_data;
		request_data = (char *)os_zalloc(os_strlen(content)+WEBSERVER_MAX_VALUE*2);
		
		os_sprintf(
			request_data,
			"%s %s HTTP/1.1\r\n"
			"Host: %s\r\n"
			"%s"
			"%s"
			"%s",
			webclient_method_str(request->method),
			request->path,
			request->host,
			request->authorization,
			request->headers,
			content
		);
		
		os_free(content);
			
#if WEBCLIENT_DEBUG
#if WEBCLIENT_VERBOSE_OUTPUT
		debug(
			"WEBCLIENT REQUEST \n"
			"\tHeaders: %d\n" 
			"\tContent: %d\n" 
			"\tLength: %d\n\n"
			"%s\n", 
			os_strlen(request->headers),
			os_strlen(request->content),
			os_strlen(request_data),
			request_data
		);
#endif
#endif

#if SSL_ENABLE
		if (connection->proto.tcp->remote_port == WEBSERVER_SSL_PORT || request->ssl) {
			espconn_secure_send(connection, request_data, os_strlen(request_data));
		} else {
			espconn_send(connection, request_data, os_strlen(request_data));
		}
#else
		espconn_send(connection, request_data, os_strlen(request_data));
#endif
			
		os_free(request_data);
	}
}

LOCAL void ICACHE_FLASH_ATTR webclient_dns(const char *name, ip_addr_t *ip, void *arg) {
	struct espconn *connection = arg;
	
	webclient_request *request = webclient_request_port_find(connection->proto.tcp->local_port);
	if (request == NULL) {
		debug(
			"WEBCLIENT: Request can not be found [%d.%d.%d.%d:%d:%d]\n", 
			IP2STR(connection->proto.tcp->remote_ip),
			connection->proto.tcp->local_port,
			connection->proto.tcp->remote_port
		);
		webclient_free_connection(connection, false);
		return;
	}
	
	if (ip == NULL) {
		debug("WEBCLIENT: Server name [%s] can not be resolved\n", name);
		webclient_error(request, connection);
		return;
	}
	
	espconn_regist_connectcb(connection, webclient_connect);
	espconn_regist_disconcb(connection, webclient_disconnect);
	espconn_regist_reconcb(connection, webclient_reconnect);
	
	*request->ip = *ip;
	
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
	debug(
		"WEBCLIENT: %s [%s] [%d.%d.%d.%d:%d:%d]\n", 
		webclient_method_str(request->method), 
		request->host, 
		IP2STR(connection->proto.tcp->remote_ip),
		connection->proto.tcp->local_port,
		connection->proto.tcp->remote_port
	);
#endif
	
	int8 conn_ret = 0;
	if (connection->proto.tcp->remote_port == WEBSERVER_SSL_PORT || request->ssl) {
#if SSL_ENABLE
		conn_ret = espconn_secure_connect(connection);
#else
		debug("WEBCLIENT: SSL is disabled\n");
#endif
	} else {
		conn_ret = espconn_connect(connection);
	}
	
	if (conn_ret != 0) {
#if CONNECTIONS_DEBUG || WEBCLIENT_DEBUG
		debug(
			"WEBCLIENT: Connect failed %d [%s]\n", 
			conn_ret,
			connection_err_str(conn_ret)
		);
#endif
		webclient_error(request, connection);
		return;
	}
}

void ICACHE_FLASH_ATTR webclient_execute(webclient_request *request) {
	if (request == NULL) {
		return;
	}
	
	if (is_websocket(request->connection)) {
		if (request->content != NULL && os_strlen(request->content) > 0) {
			websocket_send_message(EVENTS_URL, request->content, request->connection);
		}
		return;
	}
	
	if (request->disconnect_timer != 0) {
#if WEBCLIENT_DEBUG
		debug("WEBCLIENT: Pending timeout. Drop current request.\n");
#endif
		return;
	}
	
	// we want to be sure to clear request if timeout occurs
	request->disconnect_timer = setTimeout(
		(os_timer_func_t *)webclient_timeout,
		request->connection,
		WEBCLIENT_TIMEOUT
	);
		
	if (ipaddr_aton(request->host, request->ip)) {
#if WEBCLIENT_DEBUG
		debug("WEBCLIENT: No DNS needed [%s]...\n", request->host);
#endif
		webclient_dns(request->host, request->ip, request->connection);
	} else {
#if WEBCLIENT_DEBUG
		debug("WEBCLIENT: Resolving host [%s]...\n", request->host);
#endif
		espconn_gethostbyname(request->connection, request->host, request->ip, webclient_dns);
	}
}

LOCAL bool ICACHE_FLASH_ATTR webclient_host_check(char *host) {
	if (host == NULL || *host == '\0') {
#if WEBCLIENT_DEBUG
		debug("WEBCLIENT: NULL host request. Request ignored!\n");
#endif
		return false;
	}
	
	if (wifi_station_get_connect_status() != STATION_GOT_IP) {
#if WEBCLIENT_DEBUG
		debug("WEBCLIENT: Station not connected. Request ignored!\n");
#endif
		return false;
	}
	return true;
}

void ICACHE_FLASH_ATTR webclient_get(bool ssl, char *user, char *password, char *host, int port, char *path) {
	if (!webclient_host_check(host)) {
		return;
	}
	
#if WEBCLIENT_DEBUG
	debug("WEBCLIENT: New GET request\n");
#endif
	webclient_request *request = webclient_new_request(GET, ssl, user, password, host, port, path, NULL, NULL);
	webclient_execute(request);
}

void ICACHE_FLASH_ATTR webclient_post(bool ssl, char *user, char *password, char *host, int port, char *path, char *content) {
	if (!webclient_host_check(host)) {
		return;
	}
	
#if WEBCLIENT_DEBUG
	debug("WEBCLIENT: New POST request\n");
#endif
	webclient_request *request = webclient_new_request(POST, ssl, user, password, host, port, path, NULL, content);
	webclient_execute(request);
}

void ICACHE_FLASH_ATTR webclient_socket(bool ssl, char *user, char *password, char *host, int port, char *path, char *content) {
	if (!webclient_host_check(host)) {
		return;
	}
	
	uint8 key[16];
	
	char key_str[25];
	os_memset(key_str, '\0', sizeof(key_str));
	
	uint8 i;
	
	for (i=0; i<sizeof(key); i++) {
		key[i] = rand() % 256;
	}
	
	char headers[WEBSERVER_MAX_VALUE];
	
	int b64len = base64_encode(key, sizeof(key), key_str, sizeof(key_str));
	os_sprintf(
		headers, 
		"Connection: Upgrade\r\n"
		"Upgrade: websocket\r\n"
		"Origin: null\r\n"
		"Sec-WebSocket-Key: %s\r\n"
		"Sec-WebSocket-Version: 13\r\n", 
		key_str
	);
	
#if WEBCLIENT_DEBUG
	debug("WEBCLIENT: New WebSocket request\n");
#endif
	clearTimeout(reconnect_timer);
	webclient_request *request = webclient_new_request(GET, ssl, user, password, host, port, path, headers, content);
	if (request != NULL && request->state == HTTP) {
		request->state = UPGRADE;
	}
	webclient_execute(request);
}
