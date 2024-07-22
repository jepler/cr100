chargen.png: chargen
	./$< | pnmtopng > $@

chargen: chargen.c
	gcc -g $< -o $@
