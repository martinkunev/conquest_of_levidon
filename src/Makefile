O=main.o players.o menu.o world.o map.o resources.o battle.o movement.o combat.o pathfinding.o interface.o display_map.o display_common.o display_menu.o display_report.o display_battle.o input.o input_menu.o input_map.o input_battle.o input_report.o computer.o computer_map.o computer_battle.o draw.o font.o image.o format.o json.o generic/array_json.o

all: conquest_of_levidon editor

conquest_of_levidon: $(O)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

editor: editor.o world.o map.o resources.o interface.o display_common.o input.o draw.o font.o image.o format.o json.o generic/array_json.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@

units: CFLAGS:=$(CFLAGS) -DUNIT_IMPORTANCE
units: world.o map.o combat.o battle.o movement.o pathfinding.o resources.o computer.o format.o json.o generic/array_json.o
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -lm -o $@




DEPDIR:=.d
#DEPFLAGS=-MT $@ -MMD -MP -MF $(DEPDIR)/$*.Td
DEPFLAGS=-MMD -MP -MF $(DEPDIR)/$*.Td

$(shell mkdir -p $(DEPDIR) > /dev/null)

%.o : %.c $(DEPDIR)/%.d
	$(CC) $(DEPFLAGS) $(CFLAGS) -c $(OUTPUT_OPTION) $<
	mv -f $(DEPDIR)/$*.Td $(DEPDIR)/$*.d

$(DEPDIR)/%.d: ;
.PRECIOUS: $(DEPDIR)/%.d









clean:
	rm -f *.o
	rm -f conquest_of_levidon editor






-include $(patsubst %,$(DEPDIR)/%.d,$(basename $(SRCS)))
