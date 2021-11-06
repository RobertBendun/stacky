#!/usr/bin/env bash

Compiler="./stacky"
Verbose_Mode=""

Test_Count=0
Passed=0

usage() {
	echo "$(basename "$0") [subcommand] [options] files..."
	echo "  where:"
	echo "    subcommand = "
	echo "      all    - run all tests"
	echo "      one-of - run tests specified in files"
	echo
	echo "    options = "
	echo "      -v, --verbose - always print CMD messages and summary"
	echo "      -h, --help    - print this message"
	exit 1
}

cmd() {
	[ "$Verbose_Mode" ] && echo "### CMD:" $@
	$@
	Exit_Code="$?"
	[ "$Exit_Code" -ne 0 ] && {
		error "Command exited with non-zero exit code: $Exit_Code"
		exit 1
	}
}

warning() {
	echo "### WARNING:" $@
}

error() {
	echo "### ERROR:" $@
}

compile() {
	[ -f "$Compiler" ] || {
		error "Cannot find compiler at '$Compiler'"
		exit 1
	}
	cmd "$Compiler" build "$1"
}

compare() {
	cmp <($1) "$2"
	if [ "$?" -ne 0 ]; then
		error "Output of '$3' does not match expected"
	else
		let ++Passed
	fi
}

test_file() {
	Stacky_File="$1"
	Executable="${Stacky_File%%.stacky}"
	Output_File="$Executable.txt"

	[ -f "$Output_File" ] || {
		warning "file '$Stacky_File' does NOT have matching output file"
		return
	}

	compile "$Stacky_File"
	let ++Test_Count
	compare "$Executable" "$Output_File" "$Stacky_File"
}

test_directory() {
	for Stacky_File in "$1"/*.stacky; do
		test_file "$Stacky_File"
	done
}

Tests=()
Accept_Tests=""

for arg in "$@"; do
	case "$arg" in
		all)     Tests+=("tests") ;;
		one-of)  Accept_Tests=1   ;;
		-v)      Verbose_Mode=1   ;;
		--verbose) Verbose_Mode=1 ;;
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
			test_directory "$Test_Case"
		else
			test_file "$Test_Case"
		fi
	done
fi

[ "$Verbose_Mode" -o "$Test_Count" -ne "$Passed" ] && {
	echo "---------- RESULTS ----------"
	echo "Test count:  $Test_Count"
	echo "Test passed: $Passed"
}

[ "$Test_Count" -eq "$Passed" ]
