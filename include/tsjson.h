#pragma once

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
			unsigned long len;
			// valid until next tsjson_parse_* or tsjson_destroy()
			const char *data;
		} str;
		// valid for TSJSON_NUMBER
		double num;
	} u;
} tsjson_token;

typedef struct tsjson tsjson;

tsjson* tsjson_create(const char *path);
void tsjson_destroy(tsjson*);
int tsjson_parse_value(tsjson* t, tsjson_token *tok);
int tsjson_parse_dict_entry(tsjson *t, tsjson_token *tok);
int tsjson_parse_list_entry(tsjson *t, tsjson_token *tok);
int tsjson_eof(tsjson *t);

#ifdef TSJSON_IMPLEMENTATION

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void tsjson_putc(tsjson* t, int c) {
	if (t->pos >= t->capacity) {
		size_t capacity_ = (3u * t->capacity / 2u + 64u) / 64u * 64u;
		void *buffer_ = realloc(t->buffer, capacity_);
		if (!buffer_) {
			t->next = TSJSON_ST_OUT_OF_MEMORY;
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

static void tsjson_skipws(tsjson* t) {
	while (t->next > EOF && isspace(t->next))
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

static void tsjson_parse_literal(tsjson* t, const char *str) {
	const char *s = str;
	while (t->next >= 0 && *s && *s == t->next) {
		tsjson_advance(t);
		++s;
	}
	if (*s == 0 && (t->next >= EOF || isspace(t->next)))
		return;
	tsjson_error(t, "expected '%s'", str);
}

static void tsjson_parse_string(tsjson* t, tsjson_token *tok) {
	if (t->next >= EOF && t->next != '"') {
		tsjson_error(t, "expected string starting with '\"'");
		return;
	}
	tsjson_advance(t); // "
	t->pos = 0;
	for (;;) {
		if (t->next == '"') {
			tsjson_advance(t); // "
			//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
			// push null terminator
			tsjson_putc(t, 0);
			tok->u.str.data = t->buffer;
			tok->u.str.len = t->pos;
			return;
		} else if (t->next == '\\') {
			tsjson_advance(t);
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
				tsjson_putc(t, '"');
			} else if (t->next == 'u' || t->next == 'U') {
				tsjson_error(t, "unicode is not supported yet... sorry");
			} else if (t->next == EOF) {
				tsjson_error(t, "unexpected end of file");
			} else if (t->next >= 0) {
				tsjson_error(t, "invalid escaped character '%c'", t->next);
			}
			tsjson_advance(t);
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
	//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
	if (t->next >= EOF && !isdigit(t->next)) {
		tsjson_error(t, "Expected digit");
		return;
	}
	while (t->next > EOF && isdigit(t->next))
		tsjson_consume(t);
}

static void tsjson_parse_number(tsjson* t, tsjson_token *tok) {
	//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
	t->pos = 0;
	if (t->next == '-')
		tsjson_consume(t);
	if (t->next == '0')
		tsjson_consume(t);
	else
		tsjson_parse_digits(t);
	if (t->next == '.') {
		tsjson_consume(t);
		tsjson_parse_digits(t);
	}
	if (t->next == 'e' || t->next == 'E') {
		tsjson_consume(t); // eE
		if (t->next == '+' || t->next == '-') {
			tsjson_consume(t);
			tsjson_parse_digits(t);
		}
	}
	tsjson_putc(t, 0);
	if (t->next >= EOF) {
		double num;
		if (sscanf(t->buffer, "%lf", &num) == 1)
			tok->u.num = num;
		else
			tsjson_error(t, "failed to parse a number from '%s'", t->buffer);
	}
}

static int tsjson_emit(tsjson* t, tsjson_token *tok) {
	tsjson_skipws(t);
	tok->line = t->line;
	tok->col = t->col;

	if (t->next < EOF) {
		tok->tag = TSJSON_ERROR;
		tok->u.str.data = t->err;
		tok->u.str.len = strlen(t->err);
		return -1;
	}
	return 0;
}

static void tsjson_parse_value_internal(tsjson* t, tsjson_token *tok) {
	tsjson_skipws(t);
	if        (t->next == '{') {
		tok->tag = TSJSON_DICT_HEAD;
	} else if (t->next == '[') {
		tok->tag = TSJSON_LIST_HEAD;
	} else if (t->next == '"') {
		tok->tag = TSJSON_STRING;
		tsjson_parse_string(t, tok);
	} else if (t->next == '-' || (t->next >= 0 && isdigit(t->next))) {
		tok->tag = TSJSON_NUMBER;
		tsjson_parse_number(t, tok);
	} else if (t->next == 'n') {
		tok->tag = TSJSON_NULL;
		tsjson_parse_literal(t, "null");
	} else if (t->next == 't') {
		tok->tag = TSJSON_TRUE;
		tsjson_parse_literal(t, "true");
	} else if (t->next == 'f') {
		tok->tag = TSJSON_FALSE;
		tsjson_parse_literal(t, "false");
	} else if (t->next == EOF) {
		tsjson_error(t, "Unexpected end of file");
	} else if (t->next >= 0) {
		tsjson_error(t, "Unexpected character '%c'", t->next);
	}
}

static void tsjson_parse_dict_entry_internal(tsjson *t, tsjson_token *tok) {
	tsjson_skipws(t);
	//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
	if        (t->next == '}') {
		tsjson_advance(t);
		tok->tag = TSJSON_DICT_TAIL;
		return;
	} else if (t->next == '{' || t->next == ',') {
		tsjson_advance(t); //  or ,
	} else {
		tsjson_error(t, "expected ',' after entry");
		return;
	}

	tsjson_skipws(t);
	//printf("next=%c\n", t->next);
	tok->tag = TSJSON_DICT_KEY;
	tsjson_parse_string(t, tok);

	tsjson_skipws(t);
	if (t->next == ':')
		tsjson_advance(t); // ':'
	else
		tsjson_error(t, "Expected ':' after dictionary key");
	//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
}

static void tsjson_parse_list_entry_internal(tsjson *t, tsjson_token *tok) {
	tsjson_skipws(t);
	if        (t->next == ']') {
		tsjson_advance(t);
		tok->tag = TSJSON_LIST_TAIL;
		return;
	} else 	if (t->next == '[' || t->next == ',') {
		tsjson_advance(t); // [ or ,
		tsjson_parse_value_internal(t, tok);
	} else {
		tsjson_error(t, "expected ',' after entry");
	}
}

tsjson* tsjson_create(const char *path) {
	tsjson *t = malloc(sizeof *t);
	if (!t) return NULL;
	*t = (tsjson) { .buffer = NULL, .col = 1, .line = 1 };
	t->flp = fopen(path, "r");
	if (!t->flp) return tsjson_destroy(t), NULL;
	tsjson_advance(t);
	return t;
}

void tsjson_destroy(tsjson* t) {
	if (!t) return;
	if (t->flp) fclose(t->flp);
	if (t->buffer) free(t->buffer);
	free(t);
}

int tsjson_eof(tsjson *t) {
	return t->next == EOF;
}

int tsjson_parse_value(tsjson* t, tsjson_token *tok) {
	tsjson_parse_value_internal(t, tok);
	return tsjson_emit(t, tok);
}

int tsjson_parse_dict_entry(tsjson *t, tsjson_token *tok) {
	tsjson_parse_dict_entry_internal(t, tok);
	return tsjson_emit(t, tok);
}

int tsjson_parse_list_entry(tsjson *t, tsjson_token *tok) {
	tsjson_parse_list_entry_internal(t, tok);
	return tsjson_emit(t, tok);
}

#endif
