
CFLAGS = -fsanitize=address -g -Wall -Wextra -Wunused -Wunused-parameter -c

all: examples/HueVis/huevis examples/BasicColourFade/bcf

# Example: HueVis
examples/HueVis/huevis : examples/HueVis/huevis.o examples/HueVis/process_cava.o examples/HueVis/input_pulse.o examples/HueVis/input_squeezelite.o dtls.o hue_entertainment.o hue_rest.o
	gcc -fsanitize=address -lssl -lcrypto -lm -lcurl -ljson-c -lfftw3 -lpulse -lpulse-simple -lconfig -lrt -o examples/HueVis/huevis examples/HueVis/huevis.o dtls.o examples/HueVis/process_cava.o examples/HueVis/input_pulse.o examples/HueVis/input_squeezelite.o hue_entertainment.o hue_rest.o

examples/HueVis/huevis.o : examples/HueVis/huevis.c dtls.h debug.h hue_entertainment.h hue_rest.h
	gcc $(CFLAGS) examples/HueVis/huevis.c -I./ -o examples/HueVis/huevis.o

examples/HueVis/process_cava.o : examples/HueVis/process_cava.c debug.h examples/HueVis/input.h examples/HueVis/audio.h
	gcc $(CFLAGS) examples/HueVis/process_cava.c -I./ -o examples/HueVis/process_cava.o

examples/HueVis/input_pulse.o : examples/HueVis/input_pulse.c debug.h examples/HueVis/input.h examples/HueVis/audio.h
	gcc $(CFLAGS) examples/HueVis/input_pulse.c -I./ -o examples/HueVis/input_pulse.o

examples/HueVis/input_squeezelite.o : examples/HueVis/input_squeezelite.c debug.h examples/HueVis/input.h examples/HueVis/audio.h
	gcc $(CFLAGS) examples/HueVis/input_squeezelite.c -I./ -o examples/HueVis/input_squeezelite.o


# Example: BasicColourFade
examples/BasicColourFade/bcf : examples/BasicColourFade/main.o dtls.o hue_entertainment.o hue_rest.o
	gcc -fsanitize=address -lssl -lcrypto -lm -lcurl -ljson-c -o examples/BasicColourFade/bcf examples/BasicColourFade/main.o hue_entertainment.o hue_rest.o dtls.o

examples/BasicColourFade/main.o : examples/BasicColourFade/main.c dtls.h debug.h hue_entertainment.h hue_rest.h
	gcc $(CFLAGS) examples/BasicColourFade/main.c -I./ -o examples/BasicColourFade/main.o

dtls.o : dtls.c dtls.h debug.h
	gcc $(CFLAGS) dtls.c

hue_entertainment.o : hue_entertainment.c hue_entertainment.h debug.h
	gcc $(CFLAGS) hue_entertainment.c

hue_rest.o : hue_rest.c hue_rest.h debug.h
	gcc $(CFLAGS) hue_rest.c
	
clean :
	rm -f examples/HueVis/huevis examples/HueVis/*.o examples/HueVis/huevis examples/BasicColourFade/*.o examples/BasicColourFade/bcf dtls.o hue_entertainment.o hue_rest.o
	rm -rf docs/*
	rm -rf nd_conf/*

doc : dtls.h hue_entertainment.h
	naturaldocs -i ./ -o HTML ./docs -p  ./nd_conf
