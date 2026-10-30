#scope 41

#def LANG_NEG		-1
#def LANG_FLT		1.5
#def LANG_NEGFLT	-2.5
#def LANG_STR		"ko"
#def LANG_HEX		0x2A
#def LANG_CHAR		'Z'
#def LANG_TRUE		true
#def LANG_EMPTY
#def LANG_NUL		NUL
#def LANG_NULL		NULL
#def LANG_VOID		VOID
#def LANG_RE		1
#def LANG_RE		2
#def LANG_KEEP		1

#ifdef LANG_RE
#def LANG_IFDEF_OK 11

#ifndef LANG_NO_SUCH
#def LANG_IFNDEF_OK 12

#ifdef LANG_NO_SUCH
#def LANG_IFDEF_SKIP 99

#ifndef LANG_RE
#def LANG_IFNDEF_SKIP 98

#ifdef LANG_NO_SUCH
#abort

#ifndef LANG_KEEP
#abort

#include "test_language_inc.gs"

int lang_i;
float lang_f;
str lang_s;
tab lang_t;
ref lang_ref;
ptr lang_ptr;
int lang_dyn;
int lang_target;
int lang_a, lang_b;
int lang_hits;
int lang_stop_mark;

func lang_fwd, lang_fwd2;

func split_time(hours, minutes, seconds)
{
	timer = 3661;
	*hours = timer / 3600;
	*minutes = (timer / 60) % 60;
	*seconds = timer % 60;
}

func test_OutParams()
{
	h = 0;
	m = 0;
	s = 0;
	split_time(&h, &m, &s);
	return (str)h + ":" + (str)m + ":" + (str)s;
}

func bump(n)
{
	*n++;
}

func test_RefInc()
{
	n = 5;
	bump(&n);
	bump(&n);
	return n;
}

func poke(p, v)
{
	*p = v;
}

func test_TableRef()
{
	t = {10, 20, 30};
	poke(&(t[1]), 7);
	t[0]++;
	return (str)t[0] + "," + (str)t[1] + "," + (str)t[2];
}

func test_RefAlias()
{
	n = 5;
	p = &n;
	*p = 8;
	return n;
}

func test_RefIdentity()
{
	n = 0;
	p = &n;
	q = p;
	return (p == q) * 10 + (p == &n);
}

func paint(x, y, color)
{
	if(!?x) x = 1;
	if(!?y) y = 2;
	if(!?color) color = 7;
	return x * 100 + y * 10 + color;
}

func test_DefaultParams()
{
	return (str)paint() + "," + (str)paint(3) + "," + (str)paint(3, 4, 5) + "," + (str)paint(0);
}

func test_Typeof()
{
	a = 1;
	s = "ab";
	f = 1.5;
	t = {1};
	p = NULL;
	return (str)typeof(a) + (str)?s + (str)typeof(f) + (str)typeof(t) + (str)typeof(p) + (str)?undef_local;
}

func test_Sizeof()
{
	s = "hi";
	t = {1, 2, 3, 4};
	return (str)sizeof(s) + ":" + (str)strlen(s) + ":" + (str)sizeof(t);
}

func test_CastFormat()
{
	return (str "%02i")7 + "|" + (str "%03i")5 + "|" + (str "%x")255 + "|" + (str)((int)3.9) + "|" + (str)((int)(-3.9)) + "|" + (str)((int)"42") + "|" + (str "%.2f")1.5;
}

func test_CastFloat()
{
	a = (float)"1.25";
	return a + 0.75;
}

func test_CastFloatStr()
{
	return (str)((float)2);
}

func test_StringIndex()
{
	s = "AbC";
	c = s[[0]];
	s[[2]] = 'z';
	return (str)c + ":" + s;
}

func test_Constructors()
{
	s = str(4);
	t = tab(3);
	e = {};
	n = {1, 2} + {3};
	m = {{9, 8}, {7, 6}};
	return (str)sizeof(s) + ":" + (str)strlen(s) + ":" + (str)sizeof(t) + ":" + (str)typeof(t[0]) + ":" + (str)sizeof(e) + ":" + (str)sizeof(n) + ":" + (str)n[2] + ":" + (str)m[1][0];
}

func test_StringCmp()
{
	return ("ab" == "ab") + ("ab" != "ab") * 2 + ("ab" != "cd") * 4 + ("ab" == "cd") * 8;
}

func test_NullCmp()
{
	return NULL == NULL;
}

func test_Precedence()
{
	return (str)(2 + 3 * 4) + ":" + (str)(1 << 2 + 1) + ":" + (str)(8 - 3 - 1) + ":" + (str)(2 * 3 + 4 * 5);
}

func test_BitPattern()
{
	mask = 0b00010100;
	key = 2;
	flip = 2;
	dir = -1;
	hit = (mask & (1 << key)) ? 1 : 0;
	bits = (flip & 2) | (dir == -1);
	return (str)hit + ":" + (str)bits;
}

func test_DivMod()
{
	return (str)(7 / 2) + ":" + (str)(7 % 2) + ":" + (str)(-7 / 2) + ":" + (str)(-5 % 3) + ":" + (str)(-8 >> 1);
}

func test_RoomName()
{
	rx = 3;
	ry = 12;
	return "OpenRoom_" + (str)rx + "_" + (str)ry;
}

func test_Defines()
{
	return (str)LANG_NEG + "," + (str)LANG_HEX + "," + (str)LANG_CHAR + "," + (str)LANG_TRUE + "," + LANG_STR + "," + (str)LANG_RE + "," + (str)typeof(LANG_EMPTY) + "," + (str)typeof(LANG_NUL) + "," + (str)typeof(LANG_NULL) + "," + (str)typeof(LANG_VOID) + "," + (str)(LANG_NULL == NULL);
}

func test_DefineFloat()
{
	return (LANG_FLT == 1.5) + (LANG_NEGFLT == -2.5);
}

func test_Ifdef()
{
	return (str)LANG_IFDEF_OK + "," + (str)LANG_IFNDEF_OK + "," + (str)typeof(LANG_IFDEF_SKIP) + "," + (str)typeof(LANG_IFNDEF_SKIP);
}

func test_TypedDecl()
{
	return (str)typeof(lang_i) + (str)lang_i + "," + (str)typeof(lang_f) + "," + (str)typeof(lang_s) + "," + (str)typeof(lang_t) + (str)sizeof(lang_t);
}

func test_TypedAssign()
{
	lang_i = 4;
	lang_f = 1.5;
	lang_s = "zz";
	lang_t = {8, 9};
	return (str)lang_i + "," + (str)lang_f + "," + lang_s + "," + (str)lang_t[1];
}

func test_DynVar()
{
	id = gs_gid("lang_dyn");
	var(id) = 9;
	return lang_dyn;
}

func test_RefGlobal()
{
	lang_target = 4;
	lang_ref = &lang_target;
	*lang_ref = 9;
	return lang_target;
}

func test_PtrNull()
{
	lang_ptr = NULL;
	return lang_ptr == NULL;
}

func test_MultiDecl()
{
	lang_a = 3;
	lang_b = 4;
	return lang_a + lang_b;
}

func test_Forward()
{
	return lang_fwd(4) + lang_fwd2();
}

func lang_fwd(n)
{
	return n * 2;
}

func lang_fwd2()
{
	return 3;
}

func lang_empty()
{
}

func test_EmptyFn()
{
	lang_empty();
	;
	{}
	return 1;
}

func early(n)
{
	if(n < 0) return 1;
	if(n == 0) return 2;
	return 3;
}

func test_EarlyReturn()
{
	return (str)early(-1) + (str)early(0) + (str)early(8);
}

func maybe(n)
{
	if(n == 0) return;
	return n + 1;
}

func test_VoidReturn()
{
	a = 1;
	a = maybe(0);
	b = maybe(4);
	z = 3;
	z = maybe(0);
	return (str)a + ":" + (str)b + ":" + (str)z + ":" + (str)typeof(z);
}

func test_Goto()
{
	x = 1;
	goto skip;
	x = 50;
skip:
	x = x + 10;
	if(x == 11) goto done;
	x = 0;
done:
	return x;
}

func test_DoWhile()
{
	n = 0;
	do n++; while(n < 3);
	m = 0;
	do m++; while(0);
	return n * 10 + m;
}

func test_WhileSkip()
{
	n = 5;
	while(n < 3) n = 0;
	return n;
}

func test_WhileContinue()
{
	i = 0;
	n = 0;
	while(i < 5)
	{
		i++;
		if(i == 3) continue;
		n++;
	}
	return n;
}

func test_NestedBreak()
{
	sum = 0;
	for(i = 0; i < 3; i++)
	{
		for(j = 0; j < 3; j++)
		{
			if(j == 1) continue;
			if(i == 2) break;
			sum = sum + 1;
		}
	}
	return sum;
}

func test_IfNoBrace()
{
	a = 0;
	b = 1;
	if(a) b = 8; else b = 9;
	if(b == 9) b = 4;
	return b;
}

func test_BlockScope()
{
	x = 1;
	{
		x = 4;
		y = 5;
	}
	return x * 10 + y;
}

func test_LexerExtras()
{
	// line comment
	/* block
	   comment */
	a = 'A';
	b = `room`;
	c = "Open"
		"Room";
	d = "a\nb";
	e = "tab\there";
	return (str)a + ":" + b + ":" + c + ":" + (str)strlen(d) + ":" + (str)strlen(e);
}

func test_Unary()
{
	return (str)(-(3 - 8)) + ":" + (str)!!7 + ":" + (str)!!0 + ":" + (str)~0;
}

func logic_hit(v)
{
	lang_hits = lang_hits + v;
	return v;
}

func test_NoShortCircuit()
{
	lang_hits = 0;
	r = logic_hit(0) && logic_hit(1);
	r2 = logic_hit(1) || logic_hit(2);
	return (str)lang_hits + ":" + (str)r + ":" + (str)r2;
}

func test_FloatInc()
{
	f = 1.5;
	f++;
	f += 0.25;
	return f;
}

func lang_min(a, b)
{
	return (a < b) ? a : b;
}

func test_Min()
{
	return lang_min(3, 8) * 10 + lang_min(9, 2);
}

func test_HexColor()
{
	return (str)0xffffffff + ":" + (str)0xff00ff00;
}

func test_Scientific()
{
	return (str)((int)1.5e2) + ":" + (str)((int)1e2) + ":" + (str)((int)(-1.5e1));
}

func test_Breakpoint()
{
	breakpoint;
	return 1;
}

func test_LangInclude()
{
	return lang_included(4);
}

func test_Stop()
{
	lang_stop_mark = 17;
	stop;
	lang_stop_mark = 99;
	return 0;
}
