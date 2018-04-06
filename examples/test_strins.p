#include <console>
#include <string>

main()
{
	static buffer[20];

	enum eStrCmpBugfixTest
	{
		source[16],
		substr[16],
		offset,
		maxlen,
		result[16]
	};
	static tests[][eStrCmpBugfixTest] =
	{
	/*#00*/{	"abcde",	"123",	2,	8,		"ab123cd"	},
	/*#01*/{	"abcde",	"123",	0,	8,		"123abcd"	},
	/*#02*/{	"abcde",	"123",	4,	8,		"abcd123"	},
	/*#03*/{	"abcde",	"123",	5,	8,		"abcde12"	},
	/*#04*/{	"abcde",	"",		0,	8,		"abcde"		},
	/*#05*/{	"abcde",	"",		2,	8,		"abcde"		},
	/*#06*/{	"abcde",	"",		5,	8,		"abcde"		},

	/*#07*/{	!"abcde",	!"123",	2,	8 char,	!"ab123cd"	},
	/*#08*/{	!"abcde",	!"123",	0,	8 char,	!"123abcd"	},
	/*#09*/{	!"abcde",	!"123",	4,	8 char,	!"abcd123"	},
	/*#10*/{	!"abcde",	!"123",	5,	8 char,	!"abcde12"	},
	/*#11*/{	!"abcde",	!"",	0,	8 char,	!"abcde"	},
	/*#12*/{	!"abcde",	!"",	2,	8 char,	!"abcde"	},
	/*#13*/{	!"abcde",	!"",	5,	8 char,	!"abcde"	},

	/*#14*/{	"",			"123",	0,	4,		"123"		},
	/*#15*/{	"",			"123",	0,	3,		"12"		},

		// 4 char = 3 char = 1 cell
		// Packed empty string ("") is the same as an unpacked one (!""),
		// both are a 1-cell array filled with zero bytes.
		// If the "string" argument is an empty string, then the resulting
		// string is packed if "substr" is packed, and unpacked otherwise.
	/*#16*/{	!"",		!"123",	0,	4 char,	!"123"		},
	/*#17*/{	!"",		!"123",	0,	3 char,	!"123"		},
	/*#18*/{	!"",		!"123",	0,	4,		!"123"		},
	/*#19*/{	!"",		!"123",	0,	3,		!"123"		},
	/*#20*/{	"",			!"123",	0,	4,		!"123"		},
	/*#21*/{	"",			!"123",	0,	3,		!"123"		},

	/*#22*/{	"",			"",		0,	1,		""			}
	};

	new bool:success;
	new num_passed = 0, num_failed = 0;
	for (new i = 0; i < sizeof(tests); ++i)
	{
		new result_size = strlen(tests[i][result]) + 1;
		if (ispacked(tests[i][result]))
			result_size = result_size char;
		buffer[0] = '\0';
		for (new j = 1; j < sizeof(buffer); ++j)
			buffer[j] = charmax;
		strcat(buffer, tests[i][source]);
		strins(buffer, tests[i][substr], tests[i][offset], tests[i][maxlen]);
		success = true;
		for (new j = 0; j < result_size; ++j)
		{
			if (tests[i][result][j] == buffer[j])
				continue;
			success = false;
			break;
		}
		printf(
			"#%02d: [%s] strins(%s\"%s\", %s\"%s\", %d, %d): %s\"%s\" (should be %s\"%s\")\n",
			i,
			(success ? "OK" : "FAIL"),
			(ispacked(tests[i][source]) ? "!" : ""),
			tests[i][source],
			(ispacked(tests[i][substr]) ? "!" : ""),
			tests[i][substr],
			tests[i][offset],
			tests[i][maxlen],
			(ispacked(buffer) ? "!" : ""),
			buffer,
			(ispacked(tests[i][result]) ? "!" : ""),
			tests[i][result]
		);
		for (new j = result_size; j < sizeof(buffer); ++j)
		{
			if (buffer[j] == charmax)
				continue;
			printf("#%02d: Buffer overrun detected!\n", i);
			success = false;
			break;
		}
		if (!success)
		    printf(
				"Buffer contents: %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x\n",
				buffer[0], buffer[1], buffer[2], buffer[3], buffer[4],
				buffer[5], buffer[6], buffer[7], buffer[8], buffer[9],
				buffer[10], buffer[11], buffer[12], buffer[13], buffer[14],
				buffer[15], buffer[16], buffer[17], buffer[18], buffer[19]
			);
		if (success)
			num_passed++;
		else
			num_failed++;
	}
	printf(
		"%d/%d passed, %d/%d failed\n",
		num_passed, sizeof(tests),
		num_failed, sizeof(tests)
	);
}
