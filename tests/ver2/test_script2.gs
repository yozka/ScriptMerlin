
func test_Include()
{
	out = "Test Include Function";
	return out;
} 



func test_FloatToString()
{
	a = 0.5;
	b = "0.6";
	c = (str "%.01f")(a) + b;
	return c;
} 


func test_StringToFloat()
{
	a = 0.5;
	b = "0.6";
	c = a + (float)(b);
	return c;
} 
