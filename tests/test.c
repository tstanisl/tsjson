#include <tsjson.h>

#include <stdio.h>

void dump_value(tsjson* t, tsjson_token* tok);

void dump_dict(tsjson* t) {
	//puts(__func__);
	tsjson_token tok;
	while (tsjson_parse_dict_entry(t, &tok) == 0) {
		//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
		if (tok.tag == TSJSON_DICT_TAIL) {
			puts("}");
			return;
		} else if (tok.tag == TSJSON_DICT_KEY) {
			printf("key=\"%s\":", tok.str.data);
			tsjson_parse_value(t, &tok);
			printf("val=");
			dump_value(t, &tok);
		}
	}
	//printf("%s:%d: next=%c err=%s\n", __func__, __LINE__, t->next, tok.str.data);
}

void dump_list(tsjson* t) {
	//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
	tsjson_token tok;
	while (tsjson_parse_list_entry(t, &tok) == 0) {
		//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
		if (tok.tag == TSJSON_LIST_TAIL) {
			puts("]");
			return;
		}
		dump_value(t, &tok);
	}
	//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
}

void dump_value(tsjson* t, tsjson_token* tok) {
	//printf("%s:%d: next=%c\n", __func__, __LINE__, t->next);
	if (tok->tag == TSJSON_TRUE) {
		printf("true\n");
	} else if (tok->tag == TSJSON_FALSE) {
		printf("false\n");
	} else if (tok->tag == TSJSON_NULL) {
		printf("null\n");
	} else if (tok->tag == TSJSON_NUMBER) {
		printf("%g\n", tok->num);
	} else if (tok->tag == TSJSON_STRING) {
		printf("\"%s\"\n", tok->str.data);
	} else if (tok->tag == TSJSON_LIST_HEAD) {
		puts("[");
		dump_list(t);
	} else if (tok->tag == TSJSON_DICT_HEAD) {
		puts("{");
		dump_dict(t);
	} else {
		printf("tok->tag=%d r=%d c=%d err=%s\n", tok->tag, tok->line, tok->col, tok->str.data);
	}
}

int main(int argc, char *argv[]) {
	if (argc < 2) {
		fprintf(stderr, "Usage:\n\t%s path\n", argv[0]);
		return -1;
	}
	tsjson *t = tsjson_create(argv[1]);
	if (!t) {
		fprintf(stderr, "tsjson_create() failed\n");
		return -1;
	}
	tsjson_token tok;
	tsjson_parse_value(t, &tok);

	dump_value(t, &tok);

	tsjson_destroy(t);
	return 0;
}
