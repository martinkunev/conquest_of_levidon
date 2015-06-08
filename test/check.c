int main(void)
{
	extern int check_format(void);
	extern int check_json(void);

	unsigned failed = 0;
	failed += check_format();
	failed += check_json();
	return failed;
}
