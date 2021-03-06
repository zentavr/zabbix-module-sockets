#include "libzbxsockets.h"

enum
{
	TCP_CLOSE = 0x07,
	TCP_LISTEN = 0x0A
};

// supported state parameter values
static char *TCP_STATES[] = {
	"", "ESTABLISHED", "SYN_SENT", "SYN_RECV", "FIN_WAIT1",	"FIN_WAIT2",
	"TIME_WAIT", "CLOSE", "CLOSE_WAIT", "LAST_ACK", "LISTEN", "CLOSING"
};

static char *UDP_STATES[] = {
	"", "ESTABLISHED", "", "", "", "", "", "", "", "", "LISTEN", ""
};

static int isnull(const char *c, size_t n)
{
	int i = 0;
	for (i = 0; i < n && '\0' != c[i]; i++) {
		if ('0' != c[i]) {
			return 0;
		}
	}

	return 1;
}

int tcp_stat(
		AGENT_REQUEST *request,
		AGENT_RESULT *result,
		const char *path,
		int proto,
		int stat)
{
	int				res = SYSINFO_RET_FAIL, count, tx_queue_total, rx_queue_total;
	FILE			*f = NULL;
	char			buf[4096];

	char			*c, **states, *param_state;
	int				i, ok, filter_state;

	char			local_addr[128], remote_addr[128];
	int				slot, local_port, remote_port, state;
	unsigned long	tx_queue, rx_queue;

	switch (proto) {
		case IPPROTO_TCP:
			states = TCP_STATES;
			break;
		
		case IPPROTO_UDP:
			states = UDP_STATES;
			break;
		
		default:
			THIS_SHOULD_NEVER_HAPPEN;
			return SYSINFO_RET_FAIL;
	}

	if (2 < request->nparam) {
		SET_MSG_RESULT(result, strdup("Invalid number of parameters."));
		return res;
	}

	// validate state parameter
	filter_state = 0;
	if ((param_state = get_rparam(request, 1))) {
		for (c = param_state; c && *c; c++) {
			*c = toupper(*c);
		}
		for (i = 1; i < 12; i++) {
			if (0 == strncmp(states[i], param_state, 12)) {
				filter_state = i;
				break;
			}			
			if (i == 11) {
				SET_MSG_RESULT(result, strdup("Invalid TCP state."));
				return res;
			}
		}
	}

	if (NULL == (f = fopen(path, "r"))) {
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Failed to open %s: %s", path, zbx_strerror(errno)));
		return res;
	}

	count = 0;
	rx_queue_total = 0;
	tx_queue_total = 0;
	while (fgets(buf, sizeof(buf), f)) {
		if (8 == sscanf(buf, "%d: %64[0-9A-Fa-f]:%X %64[0-9A-Fa-f]:%X %X %lX:%lX %*s\n",
										&slot, local_addr, &local_port,
										remote_addr, &remote_port, &state,
										&tx_queue, &rx_queue)
		) {
			// closed udp sockets with nil remote addr:port are listening
			if (IPPROTO_UDP == proto &&
					state == TCP_CLOSE &&
					isnull(remote_addr, sizeof(remote_addr)) &&
					0 == remote_port) {
				state = TCP_LISTEN;
			}

			ok = 1;
			if (filter_state) {
				if (filter_state != state) {
					ok = 0;
				}
			}
			if (ok) {
				count++;
				tx_queue_total += tx_queue;
				rx_queue_total += rx_queue;
			}
		}
	}

	if (ferror(f)) {
		fclose(f);
		SET_MSG_RESULT(result, zbx_dsprintf(NULL, "Error reading %s: %s", path, zbx_strerror(errno)));
		return res;
	}

	fclose(f);

	switch (stat) {
		case STAT_COUNT:
			SET_UI64_RESULT(result, count);
			res = SYSINFO_RET_OK;
			break;

		case STAT_RECV_QUEUE:
			SET_UI64_RESULT(result, rx_queue_total);
			res = SYSINFO_RET_OK;
			break;
		
		case STAT_SEND_QUEUE:
			SET_UI64_RESULT(result, tx_queue_total);
			res = SYSINFO_RET_OK;
			break;
		
		default:
			THIS_SHOULD_NEVER_HAPPEN;
	}

	return res;
}
