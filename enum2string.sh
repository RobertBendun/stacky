#!/bin/sh

[ -z "$1" ] && {
	echo "usage: $0 <filename>"
	exit 1
}

sed -n '/\/\//d; /=/d; /^$/d; /enum class/,/}/p; /^struct/p' "$1" | while read -r line
do
	case "$line" in
		struct*)
			[ -z "$array_name" ] || array_name="$array_name""_"
			array_name="$array_name${line##* }"
			;;

		enum\ class*)
			printf "constexpr auto %s_%s_Names = std::array {\n" "$array_name" "${line##* }"
			unset array_name
			;;

		\{)
			;;

		\}\;)
			unset emit_comma
			printf "\n};\n"
			;;

		*)
			[ -z "$emit_comma" ] || printf ",\n"
			printf "	\"%s\"sv" "${line%%,}"
			emit_comma=1
			;;
	esac
done
