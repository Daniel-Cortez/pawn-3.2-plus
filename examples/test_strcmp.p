#include <console>
#include <string>

main()
{
	enum eStrCmpBugfixTest
	{
		str1[16],
		str2[16],
		nSym,
		shouldBe
	};
	static tests[][eStrCmpBugfixTest] =
	{
		{ "",       "",       cellmax, 0  },
		{ "",       "",       0,       0  },
		{ "",       "",       1,       0  },
		{ "",       "",       2,       0  },
		{ "abcdef", "",       cellmax, 1  },
		{ "",       "abcdef", cellmax, -1 },
		{ "abcdef", "",       0,       0  },
		{ "",       "abcdef", 0,       0  },
		{ "abcdef", "abcdef", cellmax, 0  },
		{ "abcxyz", "abcdef", cellmax, 1  },
		{ "abcdef", "abcxyz", cellmax, -1 },
		{ "abcdef", "abcdef", 0,       0  },
		{ "abcxyz", "abcdef", 0,       0  },
		{ "abcdef", "abcxyz", 0,       0  },
		{ "abcdef", "abcdef", 3,       0  },
		{ "abcxyz", "abcdef", 3,       0  },
		{ "abcdef", "abcxyz", 3,       0  },
		{ "abcdef", "abc",    cellmax, 1  },
		{ "abc",    "abcdef", cellmax, -1 },
		{ "abcdef", "abc",    5,       1  },
		{ "abc",    "abcdef", 5,       -1 }
	};
	new result;
	new num_succ = 0, num_fails = 0;
	for (new i = 0; i < sizeof(tests); ++i)
	{
		result = strcmp(tests[i][str1], tests[i][str2], false, tests[i][nSym]);
		printf(
			"strcmp(\"%s\", \"%s\", false, %d): %d (should be %d)\n",
			tests[i][str1], tests[i][str2], tests[i][nSym],
			result, tests[i][shouldBe]
		);
		if (result == tests[i][shouldBe])
			num_succ++;
		else
			num_fails++;
	}
	printf(
		"%d/%d passed, %d/%d failed\n",
		num_succ, sizeof(tests),
		num_fails, sizeof(tests)
	);
}
