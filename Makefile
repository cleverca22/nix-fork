fork.so: fork.cpp
	g++ $< -o $@ -shared

test1: fork.so
	nix-instantiate --eval test-incremental.nix --allow-unsafe-native-code-during-evaluation

.PHONY: test1
