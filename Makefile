path_helper: path_helper.c
	cc -o path_helper path_helper.c

check: path_helper
	PATH=.:$(PATH) sh check.sh

clean:
	rm -f path_helper *.log
