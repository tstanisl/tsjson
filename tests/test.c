#include <stdio.h>

enum tsjson_tag {
	TSJSON_ERROR = 0,
	TSJSON_TRUE,
	TSJSON_FALSE,
	TSJSON_NULL,
	TSJSON_STRING,
	TSJSON_NUMBER,
	TSJSON_LIST_HEAD,
	TSJSON_LIST_TAIL,
	TSJSON_DICT_HEAD,
	TSJSON_DICT_KEY,
	TSJSON_DICT_TAIL,
};

typedef struct tsjson_token {
	enum tsjson_tag tag;
	int line, col;
	union {
		// valid for TSJSON_ERROR, TSJSON_STRING, TSJSON_DICT_KEY
		struct {
			size_t len;
			// valid until next tsjson_parse_* or tsjson_destroy()
			const char *data;
		} str;
		// valid for TSJSON_NUMBER
		double num;
	};
} tsjson_token;

typedef struct tsjson tsjson;

tsjson* tsjson_create(const char *path);
void tsjson_destroy(tsjson*);
int tsjson_fetch(tsjson*, tsjson_token*);

struct tsjson {
	FILE *flp;
	int next;
	char *buffer;
	size_t capacity;
	size_t pos;
	int line, col;
	char err[128];
};

static void tsjson__consume(tsjson *t) {
	t->next = fgetc(t->flp);
	if (t->next == '\n') {
		++t->line;
		t->col = 1;
	} else {
		t->col++;
	}
}

static void tsjson__skipws(tsjson* t) {
	while (isspace(t->next))
		tsjson__consume(t);
}

static void tsjson__parse_literal(tsjson* t, tsjson_token *tok, const char *str) {
	const char *s = str;
	while (*s && *s == t->next) {
		tsjson_consume(t);
		++s;
	}
	if (*s == 0 && (t->next == EOF || isspace(t->next)))
		return;
	tok_error(t, tok, "expected '%s'", str);
}

static void tsjson__parse_string(tsjson* t, tsjson_token *tok) {
	if (t->next != '"') {
		tok_error(t, tok, "expected string starting with '\"'");
		return;
	}
	tsjson_consume(t); // "
	t->pos = 0;
	do {
		if      (t->next == '"') {
			tsjson_consume(t); // "
			// push null terminator
			tsjson_putc(tok, 0);
			tok->str.data = t->buffer;
			tok->str.len = t->pos;
			return;
		else if (t->next == '\\') {
			tsjson_consume();
			if        (t->next == 'b') {
				tsjson_putc(tok, '\b');
			} else if (t->next == 'n') {
				tsjson_putc(tok, '\n');
			} else if (t->next == 'r') {
				tsjson_putc(tok, '\r');
			} else if (t->next == 'f') {
				tsjson_putc(tok, '\f');
			} else if (t->next == '\\') {
				tsjson_putc(tok, '\\');
			} else if (t->next == '/') {
				tsjson_putc(tok, '/');
			} else if (t->next == '"') {
				tsjson_putc(tok, '\"');
			} else if (t->next == 'u' || t->next == 'U') {
				tsjson_error(t, tok, "unicode is not supported yet... sorry");
			} else {
				tsjson_error(t, tok, "invalid escaped character '%c'", t->next);
			}
		} else if (t->next == EOF) {
			tsjson_error(t, tok, "unexpected end of file");
		} else {
			tsjson_putc(tok, t->next);
		}
	} while (tok->tag != TSJSON_ERROR);
}

int tsjson_parse_value(tsjson* t, tsjson_token *tok) {
	tsjson_skipws(t);
	tok->line = t->line;
	tok->col = t->col;
	if        (t->next == '{') {
		tok->tag = TSJSON_DICT_HEAD;
	} else if (t->next == '[') {
		tok->tag = TSJSON_LIST_TAIL;
	} else if (t->next == '"') {
		tok->tag = TSJSON_STRING;
		tsjson_parse_string(t, tok);
	} else if (t->next == '-' || isdigit(t->next)) {
		tok->tag = TSJSON_NUMBER;
		tsjson_parse_number(t, tok);
	} else if (t->next == 'n') {
		tok->tag = TSJSON_NULL;
		tsjson_parse_literal(t, tok, "null");
	} else if (t->next == 't') {
		tok->tag = TSJSON_TRUE;
		tsjson_parse_literal(t, tok, "true");
	} else if (t->next == 'f') {
		tok->tag = TSJSON_FALSE;
		tsjson_parse_literal(t, tok, "false");
	} else if (t->next == EOF) {
		tsjson_error(t, tok, "Unexpected end of file");
	} else {
		tsjson_error(t, tok, "Unexpected character '%c'", t->next);
	}
	return (tok->tag == TSJSON_ERROR) ? -1 : 0;
}

int tsjson_parse_dict_entry(tsjson *t, tsjson_token *tok) {
	tsjson_skipws(t);
	tok->line = t->line;
	tok->col = t->col;
	if        (t->next == '}') {
		tsjson_consume(t);
		tok->tag = TSJSON_DICT_TAIL;
		return 0;
	}
	if (t->next != '{' && t->next != ',') {
		tsjson_error(t, tok, "expected ',' after entry");
		return -1;
	}
	tsjson_consume(t); // { or ,
	tok->tag = TSJSON_DICT_KEY;
	tsjson_parse_string(t, tok);
	if (tok->tag == TSJSON_ERROR)
		return -1;
	tsjson_skipws(t);
	if (t->next != ':') {
		tsjson_error(t, tok, "Expected ':' after dictionary key");
		return -1;
	}
	tsjson_consume(t); // ':'
	return 0;
}

int tsjson_parse_list_entry(tsjson *t, tsjson_token *tok) {
	tsjson_skipws(t);
	tok->line = t->line;
	tok->col = t->col;
	if        (t->next == ']') {
		tsjson_consume(t);
		tok->tag = TSJSON_LIST_TAIL;
		return 0;
	}
	if (t->next != '[' && t->next != ',') {
		tsjson_error(t, tok, "expected ',' after entry");
		return -1;
	}
	tsjson_consume(t); // [ or ,
	return tsjson_parse_value(t, tok);
}

tsjson* tsjson_create(const char *path) {
	tsjson *t = malloc(sizeof *t);
	if (!t) return NULL;
	*t = (tsjson) { .buffer = NULL, .col = 1, .line = 1 };
	t->flp = fopen(path, "r");
	if (!t->flp) return tsjson_destroy(t), NULL;
	return t;
}

void tsjson_destroy(tsjson* t) {
	if (!t) return;
	if (t->flp) fclose(t->flp);
	if (t->buffer) free(buffer);
	free(t);
}

#if 0

typedef struct tsjson_closure tsjson_closure;
typedef struct tsjson_event tsjson_event;

struct tsjson_closure {
	tsjson_closure (*cb)(void *ctx, tsjson_event);
	void *context;
};

tsjson_closure callback(tsjson_event *ev, void *ctx_) {
	if (ev->tag == TSJSON_LIST_START) {
		return ...;
	}
	if (ev->tag == 
}
#endif

int main() {
	return 0;
}
