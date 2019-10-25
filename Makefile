fork.so: fork.cpp fork.hh
	g++ $< -o $@ -shared -std=c++1z -g

test1: fork.so
	nix-instantiate --eval test-incremental.nix --allow-unsafe-native-code-during-evaluation --strict

.PHONY: test1
