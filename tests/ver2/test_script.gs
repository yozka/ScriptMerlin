
#include "test_script2.gs"


//
// variables
//
func test_VariableString()
{
	var1 = "This is string";
	var2 = 98;
	var4 = (str)"Test String";
	var3 = 77.85;
	return var4;
}

func test_VariableInteger()
{
	var1 = "fake var";
	i = 42;
	var2 = 89;
	return i;
}

func test_VariableFloat()
{
	f = 76.57;
	var1 = "test fake";
	return f;
}

func test_VariableBool()
{
	f = 16.54;
	var1 = "false string";
	b = true;
	return b;
}

func test_VariableBinary()
{
	bin = 0b01010110;
	return bin;
}

func test_VariableHexadecimal()
{
	hex = 0xFA73;
	return hex;
}

//
// Logic
//
func test_LogicNOT()
{
	a = false;
	b = false;
	b = !a;
	return b;
}

func test_LogicAND()
{
	a = true;
	b = true;
	c = a && b;
	return c;
}

func test_LogicOR()
{
	a = false;
	b = true;
	c = a || b;
	return c;
}

//
// ArithmeticOperator
//
func test_ArithmeticOperatorSIGN_Float()
{
	a = 68.14;
	b = -a;
	return b;
}

func test_ArithmeticOperatorSIGN_Int()
{
	a = -67;
	b = -a;
	return b;
}

func test_ArithmeticOperatorADD_FloatFloat()
{
	a = 9987.12;
	b = 136.24;
	c = a + b; 
	return c;
}

func test_ArithmeticOperatorADD_FloatInt()
{
	a = 9987.12;
	b = 138;
	c = a + b; 
	return c;
}

func test_ArithmeticOperatorADD_IntFloat()
{
	a = 998;
	b = 131.24;
	c = a + b; 
	return c;
}

func test_ArithmeticOperatorADD_IntInt()
{
	a = 24;
	b = 3;
	c = a + b; 
	return c;
}
//


func test_ArithmeticOperatorSUB_FloatFloat()
{
	a = 39337.1;
	b = 5982.93;
	c = a - b; 
	return c;
}

func test_ArithmeticOperatorSUB_FloatInt()
{
	a = 39337.1;
	b = 59;
	c = a - b; 
	return c;
}

func test_ArithmeticOperatorSUB_IntFloat()
{
	a = 6012;
	b = 5982.99;
	c = a - b; 
	return c;
}

func test_ArithmeticOperatorSUB_IntInt()
{
	a = 20;
	b = 4;
	c = a - b; 
	return c;
}


func test_ArithmeticOperatorMUL_FloatFloat()
{
	a = 54.3;
	b = 38.2;
	c = a * b; 
	return c;
}

func test_ArithmeticOperatorMUL_IntFloat()
{
	a = 50;
	b = 31.23;
	c = a * b; 
	return c;
}

func test_ArithmeticOperatorMUL_FloatInt()
{
	a = 41;
	b = 31.23;
	c = a * b; 
	return c;
}

func test_ArithmeticOperatorMUL_IntInt()
{
	a = 42;
	b = 3;
	c = a * b; 
	return c;
}

func test_ArithmeticOperatorDIV_FloatFloat()
{
	a = 44741.2;
	b = 5887.1;
	c = a / b; 
	return c;
}

func test_ArithmeticOperatorDIV_FloatInt()
{
	a = 44741.2;
	b = 5887;
	c = a / b; 
	return c;
}

func test_ArithmeticOperatorDIV_IntFloat()
{
	a = 10;
	b = 3.3;
	c = a / b; 
	return c;
}

func test_ArithmeticOperatorDIV_IntInt()
{
	a = 50;
	b = 2;
	c = a / b; 
	return c;
}
//

func test_ArithmeticOperatorMOD()
{
	a = 123;
	b = 10;
	c = a % b; 
	return c;
}

//
// BitOperator
//
func test_BitOperatorNOT()
{
	a = 0b11111111111111111111111110101001;
	b = ~a;
	return b;
}

func test_BitOperatorAND()
{
	a = 0b10111111;
	b = 0b11100111;
	c = a & b;
	return c;
}

func test_BitOperatorOR()
{
	a = 0b10111101;
	b = 0b01100101;
	c = a | b;
	return c;
}

func test_BitOperatorXOR()
{
	a = 0b10111101;
	b = 0b01100101;
	c = a ^ b;
	return c;
}

func test_BitOperatorShiftLeft()
{
	a = 13 << 1;
	return a;
}

func test_BitOperatorShiftRight()
{
	return 13 >> 2;
}

//
// AssignmentOperator
//
func test_AssignmentOperatorSet()
{
	a = 41;
	b = a;
	return b;
}

func test_AssignmentOperatorSetADD()
{
	a = 79;
	a += 5;
	return a;
}

func test_AssignmentOperatorSetSUB()
{
	a = 79;
	a -= 5;
	return a;
}

func test_AssignmentOperatorSetMUL()
{
	a = 64;
	a *= 32;
	return a;
}

func test_AssignmentOperatorSetDIV()
{
	a = 64;
	a /= 2;
	return a;
}

func test_AssignmentOperatorSetMOD()
{
	a = 4384;
	a %= 100;
	return a;
}

func test_AssignmentOperatorSetBitAND()
{
	a  = 0b10111111;
	a &= 0b01010101; 
	return a;
}

func test_AssignmentOperatorSetBitOR()
{
	a  = 0b10110111;
	a |= 0b01010101; 
	return a;
}

func test_AssignmentOperatorSetBitXOR()
{
	a  = 0b10110111;
	a ^= 0b01010101; 
	return a;
}

func test_AssignmentOperatorSetShiftLeft()
{
	a = 13;
	a <<= 2; 
	return a;
}

func test_AssignmentOperatorSetShiftRight()
{
	a = 13;
	a >>= 1; 
	return a;
}

func test_AssignmentOperatorIncrement()
{
	a = 60;
	a++;
	return a;
}

func test_AssignmentOperatorDecrement()
{
	a = 60;
	a--;
	return a;
}


//
// RelationalOperator
//
func test_RelationalOperatorEqual()
{
	a = "test1";
	b = "test1";
	return a == b;
}

func test_RelationalOperatorNotEqual()
{
	a = "test1";
	b = "test2";
	return a != b;
}

func test_RelationalOperatorLess()
{
	a = 5;
	b = 6;
	return a < b;
}

func test_RelationalOperatorLessOrEqual()
{
	a = 7;
	b = 7;
	return a <= b;
}

func test_RelationalOperatorGreater()
{
	a = 9;
	b = 8;
	return a > b;
}

func test_RelationalOperatorGreaterOrEqual()
{
	a = 10;
	b = 10;
	return a >= b;
}

func test_TernaryOperatorTrue()
{
	a = 10;
	b = 10;
	c = (a == b) ? 2 : 3;
	return c;
}

func test_TernaryOperatorFalse()
{
	a = 10;
	b = 11;
	return (a == b) ? 2 : 3;
}

func test_IndexingOperatorString()
{
	/*a = "some f text";
	return a[5];*/
	return "not_implemented";
}

func test_IndexingOperatorTable()
{
	mytable = {1,2,3,99,0};
	return mytable[3];
}

func test_BranchingIf()
{
	a = 40;
	b = 70;
	if (a < b) 
	{
		return b;
	}
	return a;
}

func test_BranchingElse()
{
	a = 40;
	b = 70;
	if (a > b) 
	{
		return b;
	}
	else
	{
		return 60;
	}
	return a;
}

func test_BranchingDeeper()
{
	a = 1;
	b = 2;
	if( a < b ) 
	{
		c = 50;
		if ( c > a )
		{
			return c;
		}
	}
	return a;
}


func test_BranchingGoto()
{
	a = 20;
	goto mylabel;
	a = 10;
	mylabel:
	return a;
}


func test_LoopingWhile()
{
	i = 0;
	while(i < 1000)
	{
		i++;
	}
	return i;
}


func test_LoopingDoWhile()
{
	i = 0;
	do 
	{
		i++;
	}
	while(i < 1010);
	return i;
}

func test_LoopingFor()
{
	k = 0;
	for(i=0; i < 1020; i++)
	{
		k++;
	}
	return k;
}

func test_LoopingForBreak()
{
	k = 0;
	for(i=0; i < 2000; i++)
	{
		k++;
		if ( k == 1030 )
		{
			break;
		}
	}
	return k;
}

func test_LoopingForContinue()
{
	k = 0;
	for(i=0; i < 2000; i++)
	{
		if (k < 1040 )
		{
			k++;
			continue;
		}
		k--;
	}
	return k;
}

func test_LoopingWhileBreak()
{
	i = 0;
	while(i < 2500)
	{
		i++;
		if (i == 1031)
		{
			break;
		}
	}
	return i;
}

func test_LoopingWhileContinue()
{
	i = 0;
	j = 0;
	while(i < 2500)
	{
		i++;
		if (j < 1032)
		{
			j++;
			continue;
		}
		j--;
	}
	return j;
}

func RetNmbers()
{
	return 321;
}

func AddTwoNumbers(a,b)
{
	return a + b;
}

func test_FunctionsCall()
{
	return RetNmbers();
}

func test_FunctionsCallParameters()
{
	k = 43;
	l = 7;
	return AddTwoNumbers(k,l);
}

func tsReturn()
{
	return 1060;
	return -111;
}
func test_FunctionsReturn()
{
	return tsReturn();
}


func MyFunction1()
{
	return 160;
}


func test_FunctionsReturnParametersZero()
{
	a = MyFunction1(); // a ZERO value is provided for b
	return a;
} 

func MyFunction2()
{
	return 161;
}

func test_FunctionsReturnBrackets()
{
	a = MyFunction2();
	return ( a ) ;
} 



// example of table parameter
func IncreaseValues(n, table)
{
	*n++;
	for(i=0;i<sizeof(table);i++)
		table[i]++;
}

func test_FunctionsChangesContent()
{
	n = 0;
	mytable = {1,2,3};
	out = "n"+(str)(n);                   // will print 0
	out += "t"+(str)(mytable[0])+","+(str)(mytable[1])+","+(str)(mytable[2])+"-";             // will print [1,2,3]
	
	IncreaseValues(&n, &mytable);
	out += "n"+(str)(n);                  // will print the same value 0
	out += "t"+(str)(mytable[0])+","+(str)(mytable[1])+","+(str)(mytable[2])+"-";          

	return out;
} 


// example of handling parameters
func SmartFunction(p1,p2)
{
	out = "";
	// p1 is handled for both number and string types
	if(typeof(p1)==TYPE_INT) // test if type is number
		out += "first parameter is number: " + (str)(p1);
	else
	if(typeof(p1)==TYPE_STR) // test if type is string
		out += "first parameter is string: " + (str)(p1);
	else
		out += "first parameter: " + (str)(p1);
	
	out += " second parameter: "+ (str)(p2);
	return out;
}

func test_FunctionsDifferentTypes()
{
	out = SmartFunction(1,2);   // both parameters: 1, 2
	out += SmartFunction("10", 0);  // second param missing: 10, 0
	return out;
} 

// example of global variables
var g_myint;
var g_mystr, g_mytab;

func DisplayGlobals()
{
	out = "";
	out += (str)(g_myint);
	out += ",";
	out += g_mystr;
	out += ",";
	out += "tab[";
	out += (str)(g_mytab[0]);
	out += ",";
	out += (str)(g_mytab[1]);
	out += ",";
	out += (str)(g_mytab[2]);
	out += "]";
	return out;
}

func ChangeAGlobal()
{
	g_myint++;
}


func test_VariablesGlobals()
{
	// first, all three global variables are ZERO
	g_mystr = "0";
	g_myint = 0;
	g_mytab = {0,0,0};
	out = DisplayGlobals();

	// initialize globals
	g_myint = 1;
	g_mystr = "test";
	g_mytab = {1,2,3};
	
	// display again
	out += DisplayGlobals();   // will print 1, test, [1,2,3]

	// do some change and display again
	ChangeAGlobal();
	out += DisplayGlobals();   // will print 2, test, [1,2,3]
	
	return out;
} 




//Constants
#def MYCOUNT 	10       // numeric constant
#def MYNAME 	"yozka"  // string define

func test_VariablesConstants()
{
	out = "";
	out += (str)(MYCOUNT);
	out += MYNAME;
	return out;
} 


// example of function variables
func MyFunc1( i )
{
 	return "MyFunc1 " + (str)(i);
}

func MyFunc2( i )
{
 	return "MyFunc2 " + (str)(i);
}

func test_VariablesFunction()
{
	out = "";
	out += MyFunc1(10);
	out += MyFunc2(10);
	return out;
} 


// example of advanced access
var g_global_access;

func test_access( param1 )
{
	return "test_access " + (str)(param1);
}
	
func test_VariablesAcess()
{
	out = "";
	g_global_access = 10;

	gid = gs_gid("g_global_access");
//	out += (str)(gs_gget(gid)) + ";";
//	gs_gset(gid, 20);
	out += (str)(g_global_access) + ";";

	functorId = gs_fid("test_access");
	out += call("ok", functorId);
	
	return out;
} 


// example of callback functions
func callback_1()
{
	return "callback_1";
}
	
func callback_2()
{
	return "callback_2";
}

func CallTheCallback( i )
{
	fnname = "callback_" + (str)(i); // form the callback's name
	gid = gs_fid(fnname);          // look for such a callback
	if(gid==-1)
		return "error";
	else
	{
		ret = call(gid);      // call the callback and receive the returned value
		return (ret);
	}
	return "Empty";
}

func test_FunctionsCallback()
{
	out = "";
	out += CallTheCallback(1);  
	out += CallTheCallback(2); 
	out += CallTheCallback(3);  //callback not found
	return out;
} 


/*
