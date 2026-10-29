

func test_Prop()
{
    obj = newobj<TStruct>();

    obj.test_int = 42;
    obj.test_str = "hello world";
    obj.run(); //  test.out = test_str + to string test_int





    return obj.test_out; //"hello world42"
}

func test_PropCall()
{
    obj = newobj<TStruct>();
    return obj.join(7, " items");
}

func test_PropAdd()
{
    obj = newobj<TStruct>();
    return obj.add(20, 22) + 1;
}

func test_PropRelease()
{
    obj = newobj<TStruct>();
    copy = obj;
    return copy.test_int;
}

func test_PropCompound()
{
    obj = newobj<TStruct>();
    obj.test_int = 10;
    obj.test_int++;
    obj.test_int += 5;
    obj.test_str = "ab";
    obj.test_str += "c";
    return (str)obj.test_int + obj.test_str;
}

