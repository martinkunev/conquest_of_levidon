O=main.o menu.o world.o map.o resources.o battle.o movement.o combat.o pathfinding.o interface.o interface_menu.o interface_report.o display.o input.o input_menu.o input_map.o input_battle.o input_report.o computer.o draw.o image.o format.o json.o generic/array_json.o

all: conquest_of_levidon

conquest_of_levidon: $(O)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

editor: image.o draw.o interface.o input.o json.o generic/array_json.o format.o editor.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

clean:
	rm -f *.o
	rm -f conquest_of_levidon editor
