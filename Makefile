fork.so: fork.cpp fork.hh
	g++ $< -o $@ -shared -std=c++1z -g -DDEBUG -fpermissive -Wall

test1: fork.so
	nix-instantiate --eval test-incremental.nix --allow-unsafe-native-code-during-evaluation --strict
test2: fork.so
	valgrind nix-instantiate --eval test-incremental2.nix --allow-unsafe-native-code-during-evaluation --strict -vvvvvvv --xml

.PHONY: test1
