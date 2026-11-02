#def ABORT_SEEN 1
func abort_seen_value()
{
	return ABORT_SEEN;
}
#abort
func abort_should_not_compile()
{
	return 12345;
}
