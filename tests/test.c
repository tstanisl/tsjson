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

enum {
	TSJSON_ST_EOF = EOF,
	TSJSON_ST_FILE_ERROR = -2,
	TSJSON_ST_OUT_OF_MEMORY = -3,
	TSJSON_ST_SYNTAX_ERROR = -4,
};

struct tsjson {
	FILE *flp;
	char *buffer;
	size_t capacity;
	size_t pos;
	//enum tsjson_state state;
	int next;
	int line, col;
	char err[128];
};

static void tsjson_advance(tsjson *t) {
	if (t->next <= EOF)
		return;
	t->next = fgetc(t->flp);
	if (t->next == EOF) {
		if (ferror(t->flp))
			t->next = TSJSON_ST_FILE_ERROR;
		return;
	}
	if (t->next == '\n') {
		++t->line;
		t->col = 1;
	} else {
		t->col++;
	}
}

static void tsjson_skipws(tsjson* t) {
	while (tsjson_isspace(t->next))
		tsjson_consume(t);
}

static void tsjson_putc(tsjson* t, int c) {
	if (t->pos >= t->capacity) {
		size_t capacity_ = (3u * t->capacity / 2u + 64u) / 64u * 64u;
		void *buffer_ = realloc(t->buffer, capacity_);
		if (!buffer_) {
			t->state = TSJSON_ST_OUT_OF_MEMORY;
			return;
		}
		t->buffer = buffer_;
		t->capacity = capacity_;
	}

	t->buffer[t->pos++] = c;
}

static void tsjson_consume(tsjson *t) {
	if (t->next > EOF)
		tsjson_putc(t, t->next);
	tsjson_advance(t);
}

static void tsjson_error(tsjson* t, const char* fmt, ...) {
	if (t->next < EOF)
		return;
	va_list va;
	va_start(va, fmt);
	vsnprintf(t->err, sizeof t->err, fmt, va);
	va_end(va);
	t->next = TSJSON_ST_SYNTAX_ERROR;
}

static void tsjson__parse_literal(tsjson* t, tsjson_token *tok, const char *str) {
	const char *s = str;
	while (t->next >= 0 && *s && *s == t->next) {
		tsjson_advance(t);
		++s;
	}
	if (*s == 0 && (t->next >= EOF || isspace(t->next)))
		return;
	tok_error(t, "expected '%s'", str);
}

static void tsjson__parse_string(tsjson* t, tsjson_token *tok) {
	if (t->next >= EOF && t->next != '"') {
		tok_error(t, "expected string starting with '\"'");
		return;
	}
	tsjson_advance(t); // "
	t->pos = 0;
	for (;;) {
		if (t->next == '"') {
			tsjson_advance(t); // "
			// push null terminator
			tsjson_putc(t, 0);
			tok->str.data = t->buffer;
			tok->str.len = t->pos;
			return;
		} else if (t->next == '\\') {
			tsjson_advance();
			if        (t->next == 'b') {
				tsjson_putc(t, '\b');
			} else if (t->next == 'n') {
				tsjson_putc(t, '\n');
			} else if (t->next == 'r') {
				tsjson_putc(t, '\r');
			} else if (t->next == 'f') {
				tsjson_putc(t, '\f');
			} else if (t->next == '\\') {
				tsjson_putc(t, '\\');
			} else if (t->next == '/') {
				tsjson_putc(t, '/');
			} else if (t->next == '"') {
				tsjson_putc(t, '\"');
			} else if (t->next == 'u' || t->next == 'U') {
				tsjson_error(t, "unicode is not supported yet... sorry");
			} else if (t->next == EOF) {
				tsjson_error(t, "unexpected end of file");
			} else if (t->next >= 0) {
				tsjson_error(t, "invalid escaped character '%c'", t->next);
			}
		} else if (t->next == EOF) {
			tsjson_error(t, "unexpected end of file");
		} else if (t->next >= 0) {
			tsjson_consume(t);
		} else {
			return;
		}
	}
}

static void tsjson_parse_digits(tsjson* t) {
	if (t->next >= EOF && !isdigit(t->next)) {
		tsjson_error(t, "Expected digit");
		return;
	}
	while (t->next > EOF && isdigit(t->next))
		tsjson_consume(t);
}

static void tsjson_parse_number(tsjson* t, tsjson_token *tok) {
	t->pos = 0;
	if (tok->next == '-')
		tsjson_consume(t);
	if (tok->next == '0')
		tsjson_consume(t);
	else
		tsjson_parse_digits(t);
	if (tok->next == '.')
		tsjson_parse_digits(t);
	if (tok->next == 'e' || tok->next == 'E') {
		tsjson_consume(t); // eE
		if (t->next == '+' || t->next == '-') {
			tsjson_consume(t);
			tsjson_parse_digits(t);
		}
	}
	tsjson_putc(t, 0);
	double num;
	if (t->next >= EOF && sscanf(t->buffer, "%lf", &num) != 1) {
		tsjson_error(t, "failed to parse a number from '%s'", t->buffer);
	} else {
		tok->num = num;
	}
}

int tsjson_emit(tsjson* t, tsjson_token *tok) {
	if (t->next < EOF) {
		tok->tag = TSJSON_ERROR;
		tok->str.data = t->err;
		tok->str.len = strlen(t->err);
		return -1;
	}
	return 0;
}

void tsjson_parse_value_internal(tsjson* t, tsjson_token *tok) {
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
	} else if (t->next >= 0) {
		tsjson_error(t, tok, "Unexpected character '%c'", t->next);
	}

int tsjson_parse_value(tsjson* t, tsjson_token *tok) {
	tsjson_parse_value_internal(t, tok);
	return tsjson_emit(t, tok);
}

void tsjson_parse_dict_entry_internal(tsjson *t, tsjson_token *tok) {
	tsjson_skipws(t);
	tok->line = t->line;
	tok->col = t->col;
	if        (t->next == '}') {
		tsjson_advance(t);
		tok->tag = TSJSON_DICT_TAIL;
		return;
	}
	if (t->next == '{' || t->next == ',') {
		tsjson_advance(t); // { or ,
	} else {
		tsjson_error(t, tok, "expected ',' after entry");
		return;
	}

	tok->tag = TSJSON_DICT_KEY;
	tsjson_parse_string(t, tok);

	tsjson_skipws(t);
	if (t->next == ':')
		tsjson_advance(t); // ':'
	else
		tsjson_error(t, tok, "Expected ':' after dictionary key");
}

int tsjson_parse_dict_entry(tsjson *t, tsjson_token *tok) {
	tsjson_parse_dict_entry_internal(t, rok);
	return tsjson_emit(t, tok);
}

void tsjson_parse_list_entry(tsjson *t, tsjson_token *tok) {
	tsjson_skipws(t);
	tok->line = t->line;
	tok->col = t->col;
	if        (t->next == ']') {
		tsjson_consume(t);
		tok->tag = TSJSON_LIST_TAIL;
		return;
	} else 	if (t->next == '[' || t->next == ',') {
		tsjson_advance(t); // [ or ,
		tsjson_parse_value_internal(t, tok);
	} else {
		tsjson_error(t, "expected ',' after entry");
	}
}

int tsjson_parse_list_entry(tsjson *t, tsjson_token *tok) {
	tsjson_parse_list_entry(t, tok);
	return tsjson_emit(t, tok);
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
