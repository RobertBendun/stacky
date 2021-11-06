#!/usr/bin/env bash

Compiler="./stacky"

Test_Count=0
Passed=0

usage() {
	echo "$(basename "$0") [subcommand] [options] files..."
	echo "  where:"
	echo "    subcommand = "
	echo "      all    - run all tests"
	echo "      one-of - run tests specified in files"
	echo "      record - record given test cases"
	echo
	echo "    options = "
	echo "      -h, --help    - print this message"
	exit 1
}

test_file() {
	Stacky_File="$1"
	Executable="${Stacky_File%%.stacky}"
	Stdout="$Executable.stdout"
	Stderr="$Executable.stderr"

	let ++Test_Count

	"$Compiler" run "$Stacky_File" > .stdout 2> .stderr

	Test_Passed=1

	if ! diff -p -N "$Stdout" .stdout; then
		Test_Passed=0
	fi

	if ! diff -N "$Stderr" .stderr; then
		Test_Passed=0
	fi

	let Passed+="$Test_Passed"
}

test_directory() {
	for Stacky_File in "$1"/*.stacky; do
		test_file "$Stacky_File"
	done
}

record_file() {
	Stacky_File="$1"
	Executable="${Stacky_File%%.stacky}"
	Stdout="$Executable.stdout"
	Stderr="$Executable.stderr"

	compile "$Stacky_File"
	echo "### Recording " "$Stacky_File"
	"$Executable" > "$Stdout" 2>"$Stderr"

	[ -s "$Stdout" ] || rm "$Stdout"
	[ -s "$Stderr" ] || rm "$Stderr"
}

record_directory() {
	for Stacky_File in "$1"/*.stacky; do
		record_file "$Stacky_File"
	done
}

Tests=()
Accept_Tests=""
Mode="test"

for arg in "$@"; do
	case "$arg" in
		all)     Tests+=("tests") ;;
		one-of)  Accept_Tests=1   ;;
		record)  Mode="record"    ;;
		-h)      usage            ;;
		--help)  usage            ;;
		*)
			if [ "$Accept_Tests" ]; then
				Tests+=("$arg")
			else
				usage
			fi
	esac
done

if [ "${#Tests[@]}" -eq 0 ]; then
	usage
else
	for Test_Case in "${Tests[@]}"; do
		if [ -d "$Test_Case" ]; then
			"$Mode"_directory "$Test_Case"
		else
			"$Mode"_file "$Test_Case"
		fi
	done
fi

if [ "$Mode" = "record" ]; then
	exit
fi

[ "$Verbose_Mode" -o "$Test_Count" -ne "$Passed" ] && {
	echo "---------- RESULTS ----------"
	echo "Test count:  $Test_Count"
	echo "Test passed: $Passed"
}

[ "$Test_Count" -eq "$Passed" ]
