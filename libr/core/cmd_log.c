/* radare - LGPL - Copyright 2009-2022 - pancake */

#include <string.h>
#include "r_config.h"
#include "r_cons.h"
#include "r_core.h"

bool rasm2_list(RCore *core, const char *arch, int fmt);

static const char *help_msg_La[] = {
	"Usage:", "La[qj]", " # asm/anal plugin list",
	"La",  "", "List asm/anal pluginsh (See rasm2 -L)",
	"Laq",  "", "Only list the plugin name",
	"Laj",  "", "Full list, but in JSON format",
	NULL
};

// TODO #7967 help refactor: move to another place
static const char *help_msg_L[] = {
	"Usage:", "L[acio]", "[-name][ file]",
	"L",  "", "show this help",
	"L", " blah."R_LIB_EXT, "load plugin file",
	"L-", "duk", "unload core plugin by name",
	"La", "[qj]", "list asm/anal plugins (see: aL, e asm.arch=" "??" ")",
	"Lc", "", "list core plugins (see",
	"Ld", "", "list debug plugins (dL)",
	"LD", "[j]", "list supported decompilers (e cmd.pdc=?)",
	"Le", "", "list esil plugins",
	"Lg", "", "list egg plugins",
	"Lh", "", "list hash plugins (ph)",
	"Li", "[j]", "list bin plugins (iL)",
	"Lt", "[j]", "list color themes (eco)",
	"Ll", "[j]", "list lang plugins (#!)",
	"LL", "", "lock screen",
	"Lm", "[j]", "list fs plugins (mL)",
	"Lo", "", "list io plugins (oL)",
	"Lp", "[j]", "list parser plugins (e asm.parser=?)",
	NULL
};

static const char *help_msg_T[] = {
	"Usage:", "T", "[-][ num|msg] # text-log utility with timestamps",
	"T", "", "list all Text log messages",
	"T", " message", "add new log message",
	"T", " 123", "list log from 123",
	"T", " 10 3", "list 3 log messages starting from 10",
	"T*", "", "list in radare commands",
	"T-", "", "delete all logs",
	"T-", " 123", "delete logs before 123",
	"Tl", "", "get last log message id",
	"Tj", "", "list in json format",
	"Tm", " [idx]", "display log messages without index",
	"TT", "", "enter into the text log chat console",
	"T=", "[.]", "pull logs from remote r2 instance specified by http.sync",
	"T=&", "", "start background thread syncing with the remote server",
	NULL
};

static void screenlock(RCore *core) {
	//  char *pass = r_cons_input ("Enter new password: ");
	char *pass = r_cons_password (Color_INVERT "Enter new password:"Color_INVERT_RESET);
	if (!pass || !*pass) {
		return;
	}
	char *again = r_cons_password (Color_INVERT "Type it again:"Color_INVERT_RESET);
	if (!again || !*again) {
		free (pass);
		return;
	}
	if (strcmp (pass, again)) {
		R_LOG_ERROR ("Password mismatch!");
		free (pass);
		free (again);
		return;
	}
	bool running = true;
	r_cons_clear_buffer ();
	ut64 begin = r_time_now ();
	ut64 last = UT64_MAX;
	int tries = 0;
	do {
		r_cons_clear00 ();
		r_cons_printf ("Retries: %d\n", tries);
		r_cons_printf ("Locked ts: %s\n", r_time_to_string (begin));
		if (last != UT64_MAX) {
			r_cons_printf ("Last try: %s\n", r_time_to_string (last));
		}
		r_cons_newline ();
		r_cons_flush ();
		char *msg = r_cons_password ("radare2 password: ");
		if (msg && !strcmp (msg, pass)) {
			running = false;
		} else {
			R_LOG_ERROR ("Invalid password");
			last = r_time_now ();
			tries++;
		}
		free (msg);
		int n = r_num_rand (10) + 1;
		r_sys_usleep (n * 100000);
	} while (running);
	r_cons_set_cup (true);
	free (pass);
	R_LOG_INFO ("Unlocked!");
}

static int textlog_chat(RCore *core) {
	char prompt[64];
	char buf[1024];
	int lastmsg = 0;
	const char *me = r_config_get (core->config, "cfg.user");
	char msg[2048];

	eprintf ("Type '/help' for commands:\n");
	snprintf (prompt, sizeof (prompt) - 1, "[%s]> ", me);
	r_line_set_prompt (prompt);
	for (;;) {
		r_core_log_list (core, lastmsg, 0, 0);
		lastmsg = core->log->last;
		if (r_cons_fgets (buf, sizeof (buf), 0, NULL) < 0) {
			return 1;
		}
		if (!*buf) {
			continue;
		}
		if (!strcmp (buf, "/help")) {
			eprintf ("/quit           quit the chat (same as ^D)\n");
			eprintf ("/name <nick>    set cfg.user name\n");
			eprintf ("/log            show full log\n");
			eprintf ("/clear          clear text log messages\n");
		} else if (!strncmp (buf, "/name ", 6)) {
			snprintf (msg, sizeof (msg) - 1, "* '%s' is now known as '%s'", me, buf + 6);
			r_core_log_add (core, msg);
			r_config_set (core->config, "cfg.user", buf + 6);
			me = r_config_get (core->config, "cfg.user");
			snprintf (prompt, sizeof (prompt) - 1, "[%s]> ", me);
			r_line_set_prompt (prompt);
			return 0;
		} else if (!strcmp (buf, "/log")) {
			r_core_log_list (core, 0, 0, 0);
			return 0;
		} else if (!strcmp (buf, "/clear")) {
			// r_core_log_del (core, 0);
			r_core_cmd0 (core, "T-");
			return 0;
		} else if (!strcmp (buf, "/quit")) {
			return 0;
		} else if (*buf == '/') {
			R_LOG_ERROR ("Unknown command: %s", buf);
		} else {
			snprintf (msg, sizeof (msg), "[%s] %s", me, buf);
			r_core_log_add (core, msg);
		}
	}
	return 1;
}

static int getIndexFromLogString(const char *s) {
	int len = strlen (s);
	const char *m = s + len;
	int nlctr = 2;
	const char *nl = NULL;
	while (m > s) {
		if (*m == '\n') {
			nl = m;
			if (--nlctr < 1) {
				return atoi (m + 1);
			}
		}
		m--;
	}
		return atoi (nl?nl + 1: s);
	return -1;
}

static char *expr2cmd(RCoreLog *log, const char *line) {
	if (!line || !*line) {
		return NULL;
	}
	line++;
	if (!strncmp (line, "add-comment", 11)) {
		line += 11;
		if (*line == ' ') {
			char *sp = strchr (line + 1, ' ');
			if (sp) {
				char *msg = sp + 1;
				ut64 addr = r_num_get (NULL, line);
				return r_str_newf ("CCu base64:%s @ 0x%"PFMT64x"\n", msg, addr);
			}
		}
		R_LOG_ERROR ("Cannot parse add-comment expression");
	}
	if (!strncmp (line, "del-comment", 11)) {
		if (line[11] == ' ') {
			return r_str_newf ("CC-%s\n", line + 12);
		}
		R_LOG_ERROR ("Cannot parse add-comment expression");
	}
	return NULL;
}

static int log_callback_r2(RCore *core, int count, const char *line) {
	if (*line == ':') {
		char *cmd = expr2cmd (core->log, line);
		if (cmd) {
			r_cons_printf ("%s\n", cmd);
			r_core_cmd (core, cmd, 0);
			free (cmd);
		}
	}
	return 0;
}

static int log_callback_all(RCore *log, int count, const char *line) {
	r_cons_printf ("%d %s\n", count, line);
	return 0;
}

static int cmd_log(void *data, const char *input) {
	RCore *core = (RCore *) data;
	const char *arg, *input2;
	int n, n2;

	if (!input) {
		return 1;
	}

	input2 = (input && *input)? input + 1: "";
	arg = strchr (input2, ' ');
	n = atoi (input2);
	n2 = arg? atoi (arg + 1): 0;

	switch (*input) {
	case 'e': // "Te" shell: less
		{
			char *p = strchr (input, ' ');
			if (p) {
				char *b = r_file_slurp (p + 1, NULL);
				if (b) {
					r_cons_less_str (b, NULL);
					free (b);
				} else {
					R_LOG_ERROR ("File not found");
				}
			} else {
				eprintf ("Usage: less [filename]\n");
			}
		}
		break;
	case 'l': // "Tl"
		r_cons_printf ("%d\n", core->log->last - 1);
		break;
	case '-': //  "T-"
		r_core_log_del (core, n);
		break;
	case '?': // "T?"
		r_core_cmd_help (core, help_msg_T);
		break;
	case 'T': // "TT" Ts ? as ms?
		if (r_cons_is_interactive ()) {
			textlog_chat (core);
		} else {
			R_LOG_ERROR ("The TT command needs scr.interactive=true");
		}
		break;
	case '=': // "T="
		if (input[1] == '&') { //  "T=&"
			if (input[2] == '&') { // "T=&&"
				r_cons_break_push (NULL, NULL);
				while (!r_cons_is_breaked ()) {
					r_core_cmd0 (core, "T=");
					void *bed = r_cons_sleep_begin();
					r_sys_sleep (1);
					r_cons_sleep_end (bed);
				}
				r_cons_break_pop ();
			} else {
				// TODO: Sucks that we can't enqueue functions, only commands
				R_LOG_INFO ("Background thread syncing with http.sync started");
				RCoreTask *task = r_core_task_new (core, true, "T=&&", NULL, core);
				r_core_task_enqueue (&core->tasks, task);
			}
		} else {
			if (atoi (input + 1) > 0 || (input[1] == '0')) {
				core->sync_index = 0;
			} else {
				RCoreLogCallback log_callback = (input[1] == '*')
					? log_callback_all: log_callback_r2;
				char *res = r_core_log_get (core, core->sync_index);
				if (res) {
					int idx = getIndexFromLogString (res);
					if (idx != -1) {
						core->sync_index = idx + 1;
					}
					r_core_log_run (core, res, log_callback);
					free (res);
				} else {
					r_cons_printf ("Please check e http.sync\n");
				}
			}
		}
		break;
	case ' ': // "T "
		if (n > 0 || *input == '0') {
			r_core_log_list (core, n, n2, *input);
		} else {
			r_core_log_add (core, input + 1);
		}
		break;
	case 'm': // "Tm"
		if (n > 0) {
			r_core_log_list (core, n, 1, 't');
		} else {
			r_core_log_list (core, n, 0, 't');
		}
		break;
	case 'j': // "Tj"
	case '*':
	case '\0':
		r_core_log_list (core, n, n2, *input);
		break;
	}
	return 0;
}

static int cmd_plugins(void *data, const char *input) {
	RCore *core = (RCore *) data;
	switch (input[0]) {
	case 0:
		r_core_cmd_help (core, help_msg_L);
		// return r_core_cmd0 (core, "Lc");
		break;
	case '-':
		r_lib_close (core->lib, r_str_trim_head_ro (input + 1));
		break;
	case ' ':
		r_lib_open (core->lib, r_str_trim_head_ro (input + 1));
		break;
	case '?':
		r_core_cmd_help (core, help_msg_L);
		break;
	case 't': // "Lt"
		if (input[1] == 'j') {
			r_core_cmd0 (core, "ecoj");
		} else {
			r_core_cmd0 (core, "eco");
		}
		break;
	case 'm': // "Lm"
		if (input[1] == 'j') {
			r_core_cmd0 (core, "mLj");
		} else {
			r_core_cmd0 (core, "mL");
		}
		break;
	case 'e': // "Le"
		r_core_cmdf (core, "aeL%s", input + 1);
		break;
	case 'd': // "Ld"
		r_core_cmdf (core, "dL%s", input + 1);
		break;
	case 'h': // "Lh"
		if (input[1] == 'j') { // "Lhj"
			r_core_cmd0 (core, "phj");
		} else {
			r_core_cmd0 (core, "ph");
		}
		break;
	case 'a': // "La"
		if (input[1] == '?') {
			r_core_cmd_help (core, help_msg_La);
		} else {
			// r_core_cmd0 (core, "e asm.arch=??");
			rasm2_list (core, NULL, input[1]);
		}
		break;
	case 'p': // "Lp"
		if (input[1] == 'j') { // "Lpj"
			RConfigNode *node = r_config_node_get (core->config, "asm.parser");
			if (node && node->options) {
				char *opt;
				RListIter *iter;
				PJ *pj = r_core_pj_new (core);
				pj_a (pj);
				r_list_foreach (node->options, iter, opt) {
					pj_s (pj, opt);
				}
				pj_end (pj);
				char *s = pj_drain (pj);
				r_cons_printf ("%s\n", s);
				free (s);
			}
		} else {
			r_core_cmd0 (core, "e asm.parser=?");
		}
		break;
	case 'D': // "LD"
		if (input[1] == ' ') {
			r_core_cmdf (core, "e cmd.pdc=%s", r_str_trim_head_ro (input + 2));
		} else if (input[1] == 'j') {
			char *deco;
			RListIter *iter;
			char *decos = r_core_cmd_str (core, "e cmd.pdc=?");
			RList *list = r_str_split_list (decos, "\n", 0);
			PJ *pj = r_core_pj_new (core);
			pj_o (pj);
			pj_ka (pj, "decompilers");
			r_list_foreach (list, iter, deco) {
				if (*deco) {
					pj_s (pj, deco);
				}
			}
			pj_end (pj);
			pj_end (pj);
			char *s = pj_drain (pj);
			r_cons_printf ("%s\n", s);
			free (s);
			r_list_free (list);
			free (decos);
		} else {
			r_core_cmd0 (core, "e cmd.pdc=?");
		}
		break;
	case 'l': // "Ll"
		if (input[1] == 'j') {
			r_core_cmd0 (core, "#!?j");
		} else {
			r_core_cmd0 (core, "#!");
		}
		break;
	case 'L': // "LL"
		if (r_config_get_b (core->config, "scr.interactive")) {
			screenlock (core);
		} else {
			R_LOG_ERROR ("lock screen requires scr.interactive");
		}
		break;
	case 'g': // "Lg"
		if (input[1] == 'j') {
			r_core_cmd0 (core, "gLj");
		} else {
			r_core_cmd0 (core, "gL");
		}
		break;
	case 'o': // "Lo"
	case 'i': // "Li"
		r_core_cmdf (core, "%cL%s", input[0], input + 1);
		break;
	case 'c': { // "Lc"
		RListIter *iter;
		RCorePlugin *cp;
		switch (input[1]) {
		case 'j': {
			PJ *pj = r_core_pj_new (core);
			if (!pj) {
				return 1;
			}
			pj_a (pj);
			r_list_foreach (core->rcmd->plist, iter, cp) {
				pj_o (pj);
				pj_ks (pj, "Name", cp->name);
				pj_ks (pj, "Description", cp->desc);
				pj_end (pj);
			}
			pj_end (pj);
			r_cons_println (pj_string (pj));
			pj_free (pj);
			break;
			}
		case ' ':
			r_lib_open (core->lib, r_str_trim_head_ro (input + 2));
			break;
		case 0:
			r_lib_list (core->lib);
			r_list_foreach (core->rcmd->plist, iter, cp) {
				r_cons_printf ("%s: %s\n", cp->name, cp->desc);
			}
			break;
		default:
			r_core_cmd_help (core, help_msg_L);
			break;
		}
		}
		break;
	}
	return 0;
}
