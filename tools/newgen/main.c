#include "globals.h"
#include "parser.h"
#include <ctype.h>
#include <errno.h>

#include "iburgcodes.h"

static Symbol* symbol_table = NULL;
static const char* SYM_REGISTER = "register";
static const char* SYM_REGCLASS = "register class";

static Rule* rules[500];
static int rulescount = 0;
static int registercount = 0;

static int maxdepth = 0;
static Node* pattern;

static const char* infilename;
FILE* outfp;
FILE* outhfp;

void* lookup_symbol(const char* name, const char* kind)
{
	Symbol* s = symbol_table;
	while (s)
	{
		if (strcmp(s->name, name) == 0)
		{
			if (kind && (s->kind != kind))
				fatal("symbol '%s' is not a %s", name, kind);
			return s;
		}
		s = s->next;
	}
	return NULL;
}

void* define_symbol(const char* name, const char* kind, size_t size)
{
	if (lookup_symbol(name, kind))
		fatal("symbol '%s' already exists", name);
	Symbol* s = calloc(size, 1);
	s->name = name;
	s->kind = kind;
	s->next = symbol_table;
	symbol_table = s;
	return s;
}

Register* define_register(const char* name)
{
	Register* reg = define_symbol(name, SYM_REGISTER, sizeof(Register));
	reg->compatible = reg->uses = reg->id = 1<<registercount;
	registercount++;
	return reg;
}

Register* lookup_register(const char* name)
{
	Register* reg = lookup_symbol(name, SYM_REGISTER);
	if (!reg)
		fatal("unknown register '%s'", name);
	return reg;
}

Register* lookup_register_by_id(reg_t id)
{
	Register* reg = (Register*) symbol_table;
	while (reg)
	{
		if ((reg->sym.kind == SYM_REGISTER) && (reg->id == id))
			return reg;
		reg = (Register*) reg->sym.next;
	}
	fatal("unknown register 0x%x", id);
}

reg_t lookup_register_or_class(const char* name)
{
	Symbol* s = lookup_symbol(name, NULL);
	if (s)
	{
		if (s->kind == SYM_REGISTER)
			return ((Register*)s)->id;
		if (s->kind == SYM_REGCLASS)
			return ((RegisterClass*)s)->reg;
	}
	fatal("'%s' is not a register or register class", name);
}

void define_regclass(const char* name, reg_t reg)
{
	RegisterClass* rc = define_symbol(name, SYM_REGCLASS, sizeof(RegisterClass));
	rc->reg = reg;
}

int lookup_midcode(const char* name)
{
	/* TODO: these are not terminals, change the name */
	for (int i=0; i<sizeof(terminals)/sizeof(*terminals); i++)
	{
		const char* t = terminals[i];
		if (strcmp(name, t) == 0)
			return i; 
	}
	yyerror("unknown midcode '%s'", name);
	return 0;
}

Node* register_matcher(reg_t reg, Label* label)
{
	Node* n = calloc(sizeof(Node), 1);
	n->isregister = true;
	n->reg = reg;
	n->label = label;
	return n;
}

Node* tree_matcher(int midcode, Node* left, Node* right, Predicate* predicate, Label* label)
{
	Node* n = calloc(sizeof(Node), 1);
	n->isregister = false;
	n->midcode = midcode;
	n->left = left;
	n->right = right;
	n->predicate = predicate;
	n->label = label;
	return n;
}

Rule* rewriterule(int lineno, Node* pattern, Node* replacement)
{
	Rule* r = calloc(sizeof(Rule), 1);
	r->lineno = lineno;
	r->pattern = pattern;
	r->replacement = replacement;

	if (rulescount == (sizeof(rules)/sizeof(*rules)))
		yyerror("too many rules");
	rules[rulescount++] = r;
	return r;
}

Rule* genrule(int lineno, Node* pattern, reg_t result)
{
	Rule* r = calloc(sizeof(Rule), 1);
	r->lineno = lineno;
	r->pattern = pattern;
	r->result_reg = result;

	if (rulescount == (sizeof(rules)/sizeof(*rules)))
		yyerror("too many rules");
	rules[rulescount++] = r;
	return r;
}

static int collect_template_data(Node* template, Node* pattern, Label** last_label)
{
	if (!template)
		return 0;
	if (template->label)
	{
		template->label->next = *last_label;
		*last_label = template->label;
	}

	int cost = !(template->isregister) + !!(template->predicate);
	if (template->left)
	{
		if (!pattern->left)
		{
			Node* p = calloc(sizeof(Node), 1);
			pattern->left = p;
		}
		cost += collect_template_data(template->left, pattern->left, last_label);
	}
	if (template->right)
	{
		if (!pattern->right)
		{
			Node* p = calloc(sizeof(Node), 1);
			pattern->right = p;
		}
		cost += collect_template_data(template->right, pattern->right, last_label);
	}
	return cost;
}

static int sort_rule_cb(const void* left, const void* right)
{
	const Rule* const* r1 = left;
	const Rule* const* r2 = right;
	if ((*r1)->cost > (*r2)->cost)
		return -1;
	if ((*r1)->cost < (*r2)->cost)
		return 1;
	return 0;
}

static void calculate_pattern_size(Node* node)
{
	maxdepth++;
	if (node->left)
		calculate_pattern_size(node->left);
	if (node->right)
		calculate_pattern_size(node->right);
}

static Node* lookup_label(Node* node, const char* name)
{
	Node* f = NULL;
	if (!node)
		return f;
	else if (node->label && (strcmp(node->label->name, name) == 0))
		f = node;
	else
	{
		f = lookup_label(node->left, name);
		if (!f)
			f = lookup_label(node->right, name);
	}
	return f;
}

static void resolve_label_names(Rule* rule)
{
	Label* label = rule->first_label;
	while (label)
	{
		/* Check the name of this label doesn't conflict with any other
		 * on the rule. */

		Label* other = label->next;
		while (other)
		{
			if (strcmp(label->name, other->name) == 0)
				fatal("duplicate label '%s'", label->name);
			other = other->next;
		}

		label = label->next;
	}
}

static void assign_indices_to_nodes(int* offset, Node* template, Node* pattern)
{
	if (template)
		template->index = *offset;

	if (pattern->left)
	{
		(*offset)++;
		assign_indices_to_nodes(offset, template ? template->left : NULL, pattern->left);
	}
	if (pattern->right)
	{
		(*offset)++;
		assign_indices_to_nodes(offset, template ? template->right : NULL, pattern->right);
	}
}

static void sort_rules(void)
{
	pattern = calloc(sizeof(Node), 1);

	for (int i=0; i<rulescount; i++)
	{
		Rule* r = rules[i];
		r->cost = collect_template_data(r->pattern, pattern, &r->first_label);
	}

	qsort(rules, rulescount, sizeof(Rule*), sort_rule_cb);

	calculate_pattern_size(pattern);

	for (int i=0; i<rulescount; i++)
	{
		Rule* r = rules[i];

		int offset = 0;
		assign_indices_to_nodes(&offset, r->pattern, pattern);

		resolve_label_names(r);

		r->compatible_regs = 0;
		Register* reg = (Register*) symbol_table;
		while (reg)
		{
			if ((reg->sym.kind == SYM_REGISTER) && (r->result_reg & reg->id))
				r->compatible_regs |= reg->compatible;
			reg = (Register*) reg->sym.next;
		}
	}
}

static void print_upper(FILE* fp, const char* s)
{
	while (*s)
		fputc(toupper(*s++), fp);
}

static void dump_registers(void)
{
	fprintf(outfp, "const Register registers[] = {\n");
	fprintf(outhfp, "enum {\n");
	Register* reg = (Register*) symbol_table;
	while(reg)
	{
		if (reg->sym.kind == SYM_REGISTER)
		{
			fprintf(outfp, "\t{ \"%s\", 0x%x, 0x%x, 0x%x, %d },\n",
				reg->sym.name, reg->id, reg->uses, reg->compatible, reg->isstacked);
			fprintf(outhfp, "\tREG_");
			print_upper(outhfp, reg->sym.name);
			fprintf(outhfp, " = 0x%x,\n", reg->id);
		}
		reg = (Register*) reg->sym.next;
	}
	fprintf(outfp, "};\n");
	fprintf(outhfp, "};\n");
}

static void walk_template_tree(int* offset, Node* template, Node* pattern)
{
	int thisoffset = *offset;
	fprintf(outfp, "%d, ", template ? template->midcode : 0);

	if (pattern->left)
	{
		(*offset)++;
		walk_template_tree(offset, template ? template->left : NULL, pattern->left);
	}
	if (pattern->right)
	{
		(*offset)++;
		walk_template_tree(offset, template ? template->right : NULL, pattern->right);
	}
}

static void dump_templates(void)
{
	for (int i=0; i<rulescount; i++)
	{
		Rule* r = rules[i];
		fprintf(outfp, "static const uint8_t template%d[%d] = { ", i, maxdepth);
		int offset = 0;
		walk_template_tree(&offset, r->pattern, pattern);
		fprintf(outfp, "}; /* cost %d */\n", r->cost);
	}
}

static void walk_matcher_tree(int* offset, Node* pattern)
{
	int thisoffset = *offset;
	fprintf(outfp, "\tmatchbuf[%d] = n%d->op;\n", thisoffset, thisoffset);

	if (pattern->left)
	{
		(*offset)++;
		fprintf(outfp, "\tn%d = n%d->left;\n", *offset, thisoffset);
		fprintf(outfp, "\tif (n%d) {\n", *offset);
		walk_matcher_tree(offset, pattern->left);
		fprintf(outfp, "\t}\n");
	}
	if (pattern->right)
	{
		(*offset)++;
		fprintf(outfp, "\tn%d = n%d->right;\n", *offset, thisoffset);
		fprintf(outfp, "\tif (n%d) {\n", *offset);
		walk_matcher_tree(offset, pattern->right);
		fprintf(outfp, "\t}\n");
	}
}

static const char* operator_name(int operator)
{
	switch (operator)
	{
		case EQUALS: return "==";
		case NOTEQUALS: return "!=";
	}
	assert(false);
}

static void print_lower(const char* s)
{
	while (*s)
		fputc(tolower(*s++), outfp);
}

static void print_predicate(int index, Node* template, Predicate* predicate)
{
	while (predicate)
	{
		switch (predicate->operator)
		{
			case IS:
				fprintf(outfp, " && is_%s(n%d->u.", predicate->u.callback, index);
				print_lower(terminals[template->midcode]);
				fprintf(outfp, ".%s)", predicate->field);
				break;

			default:
				fprintf(outfp, " && (n%d->u.", index);
				print_lower(terminals[template->midcode]);
				fprintf(outfp, ".%s %s %d)",
					predicate->field,
					operator_name(predicate->operator),
					predicate->u.value);
				break;
		}

		predicate = predicate->next;
	}
}

static void walk_predicate_tree(int* offset, Node* template, Node* pattern)
{
	int thisoffset = *offset;
	if (template)
		print_predicate(thisoffset, template, template->predicate);

	if (pattern->left)
	{
		(*offset)++;
		walk_predicate_tree(offset, template ? template->left : NULL, pattern->left);
	}
	if (pattern->right)
	{
		(*offset)++;
		walk_predicate_tree(offset, template ? template->right : NULL, pattern->right);
	}
}

static void push_nodes(int* offset, Node* template, Node* pattern)
{
	int thisoffset = *offset;
	if (template && template->isregister)
	{
		fprintf(outfp, "\t\tn%d->desired_reg = 0x%x;\n", thisoffset, template->reg);
		fprintf(outfp, "\t\tn%d->consumer = insn;\n", thisoffset);
		fprintf(outfp, "\t\tpush_node(n%d);\n", thisoffset);
	}

	if (pattern->left)
	{
		(*offset)++;
		push_nodes(offset, template ? template->left : NULL, pattern->left);
	}
	if (pattern->right)
	{
		(*offset)++;
		push_nodes(offset, template ? template->right : NULL, pattern->right);
	}
}

static void print_complex_action(Rule* r, Element* e)
{
	if (e->next)
		print_complex_action(r, e->next);

	if (e->islabel)
	{
		if (e->text[0] == '$')
			fprintf(outfp, "self->produced_reg");
		else
		{
			Node* node = lookup_label(r->pattern, e->text);
			if (!node)
				fatal("nothing labelled '%s' at line %d", e->text, r->lineno);

			if (node->isregister)
				fprintf(outfp, "self->n[%d]->produced_reg", node->index);
			else
			{
				fprintf(outfp, "self->n[%d]->u.", node->index);
				print_lower(terminals[node->midcode]);
			}
		}
	}
	else
		fprintf(outfp, "%s", e->text);
}

static void print_simple_action(Rule* r, Element* e)
{
	fatal("simple actions not supported yet");
}

static void print_line(int lineno)
{
	fprintf(outfp, "#line %d \"%s\"\n", lineno+1, infilename);
}

static void create_emitters(void)
{
	fprintf(outfp, "void emit_instruction(Instruction* self) {\n");
	fprintf(outfp, "\tswitch (self->rule) {\n");

	for (int i=0; i<rulescount; i++)
	{
		fprintf(outfp, "\t\tcase %d:\n", i);
		fprintf(outfp, "\t\t{\n");

		Rule* r = rules[i];
		print_line(r->lineno);

		Action* a = r->action;
		if (a)
		{
			if (a->iscomplex)
				print_complex_action(r, a->first_element);
			else
				print_simple_action(r, a->first_element);
		}

		fprintf(outfp, "\n\t\t\tbreak;\n\t\t}\n");
	}

	fprintf(outfp, "\t}\n");
	fprintf(outfp, "}\n");
}

static void emit_replacement(Rule* rule, Node* pattern, Node* replacement)
{
	if (replacement->isregister)
	{
		Node* node = lookup_label(pattern, replacement->label->name);
		if (!node)
			fatal("nothing labelled '%s' at line %d",
				replacement->label->name, rule->lineno);

		fprintf(outfp, "n%d", node->index);
	}
	else
	{
		fprintf(outfp, "mid_");
		print_lower(terminals[replacement->midcode]);
		fprintf(outfp, "(");

		if (replacement->left)
		{
			emit_replacement(rule, pattern, replacement->left);
			if (replacement->right)
			{
				fprintf(outfp, ", ");
				emit_replacement(rule, pattern, replacement->right);
			}
		}
		fprintf(outfp, ")");
	}
}

static void emit_node_copiers(int* offset, Node* template, Node* pattern)
{
	int thisoffset = *offset;
	if (template)
		fprintf(outfp, "\t\tinsn->n[%d] = n%d;\n", thisoffset, thisoffset);

	if (pattern->left)
	{
		(*offset)++;
		emit_node_copiers(offset, template ? template->left : NULL, pattern->left);
	}
	if (pattern->right)
	{
		(*offset)++;
		emit_node_copiers(offset, template ? template->right : NULL, pattern->right);
	}
}

static void create_matcher(void)
{
	fprintf(outfp, "void match_instruction(Node* n0, Instruction* insn) {\n");
	fprintf(outfp, "again:;\n");
	fprintf(outfp, "\tstatic uint8_t matchbuf[%d] = {0};\n", maxdepth);

	for (int i=1; i<maxdepth; i++)
		fprintf(outfp, "\tNode* n%d = NULL;\n", i);

	int offset = 0;
	walk_matcher_tree(&offset, pattern);

	for (int i=0; i<rulescount; i++)
	{
		Rule* r = rules[i];
		print_line(r->lineno);
		if (r->replacement)
		{
			fprintf(outfp, "\tif (");
		}
		else
		{
			/* Only match this rule if the destination register is appropriate. */
			if (!r->compatible_regs)
				fprintf(outfp, "\tif (!n0->desired_reg &&\n\t\t");
			else
				fprintf(outfp, "\tif ((n0->desired_reg & 0x%x) &&\n\t\t", r->compatible_regs);
		}

		fprintf(outfp, "template_comparator(matchbuf, template%d)", i, maxdepth);
		offset = 0;
		walk_predicate_tree(&offset, r->pattern, pattern);
		fprintf(outfp, ") {\n");

		if (r->replacement)
		{
			fprintf(outfp, "\t\tNode* n = ");
			emit_replacement(r, r->pattern, r->replacement);
			fprintf(outfp, ";\n");
			fprintf(outfp, "\t\tn->desired_reg = n0->desired_reg;\n");
			fprintf(outfp, "\t\tn0 = n;\n");
			fprintf(outfp, "\t\tgoto again;\n");
		}
		else
		{
			fprintf(outfp, "\t\tinsn->rule = %d;\n", i);
			fprintf(outfp, "\t\tinsn->producable_regs = 0x%x;\n", r->result_reg);
			if (r->uses_regs)
				fprintf(outfp, "\t\tinsn->output_regs = 0x%x;\n", r->uses_regs);

			offset = 0;
			emit_node_copiers(&offset, r->pattern, pattern);

			offset = 0;
			push_nodes(&offset, r->pattern, pattern);
		}

		fprintf(outfp, "\t} else\n");
	}

	fprintf(outfp, "\t\tunmatched_instruction(n0);\n");
	fprintf(outfp, "}\n");
}

int main(int argc, const char* argv[])
{
	if (argc != 4)
		fatal("syntax: newgen <inputfile> <output c file> <output h file>");

	infilename = argv[1];
	FILE* infp = fopen(infilename, "r");
	if (!infp)
		fatal("cannot open input file '%s': %s", infilename, strerror(errno));

	outfp = fopen(argv[2], "w");
	if (!outfp)
		fatal("cannot open output C file '%s': %s", argv[2], strerror(errno));

	outhfp = fopen(argv[3], "w");
	if (!outhfp)
		fatal("cannot open output H file '%s': %s", argv[3], strerror(errno));


	include_file(open_file(infp));
	parse();

	sort_rules();

	fprintf(outhfp, "#ifndef NEWGEN_H\n");
	fprintf(outhfp, "#define NEWGEN_H\n");
	fprintf(outhfp, "#define INSTRUCTION_TEMPLATE_DEPTH %d\n", maxdepth);
	fprintf(outhfp, "#define REGISTER_COUNT %d\n", registercount);

	dump_registers();
	dump_templates();
	create_emitters();
	create_matcher();

	fprintf(outhfp, "#endif\n");

	return errcnt>0;
}
