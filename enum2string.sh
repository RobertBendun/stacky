#!/bin/sh

[ -z "$1" ] && {
	echo "usage: $0 <filename>"
	exit 1
}

join() {
	prefix=""
	[ "$1" ] && prefix="_"
	printf "%s" "$1$prefix$2"
}

unset emit_newline
sed -n '/\/\//d; /=/d; /^$/d; /enum class/,/}/p; /^struct/p' "$1" | while read -r line
do
	case "$line" in
		struct*)
			array_name="$(join "$array_name" "${line##* }")"
			;;

		enum\ class*)
			[ "$emit_newline" ] && echo
			printf "constexpr auto %s_Names = std::array {\n" "$(join "$array_name" "${line##* }")"
			unset array_name
			;;

		\{)
			;;

		\}\;)
			unset emit_comma
			emit_newline=1
			printf "\n};\n"
			;;

		*)
			[ "$emit_comma" ] && printf ",\n"
			printf "	\"%s\"sv" "${line%%,}"
			emit_comma=1
			;;
	esac
done
