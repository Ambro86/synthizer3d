set -e
for f in $(find src include examples test benchmarks -path src/data -prune -o -type f -print); do
	clang-format -style=file -i $f
done
