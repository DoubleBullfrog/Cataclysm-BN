#pragma once

class monster;

namespace monexamine
{
bool pet_menu( monster &z );
bool mech_hack( monster &z );
bool pay_bot( monster &z );
bool mfriend_menu( monster &z );
void remove_battery( monster &z );
void insert_battery( monster &z );
void push( monster &z );
void rename_pet( monster &z );
void attach_bag_to( monster &z );
void remove_bag_from( monster &z );
void dump_items( monster &z );
bool give_items_to( monster &z );
void take_items_from( monster &z );
bool add_armor( monster &z );
void remove_armor( monster &z );
void remove_harness( monster &z );
void play_with( monster &z );
void kill_zslave( monster &z );
void add_leash( monster &z );
void remove_leash( monster &z );
void start_leading( monster &z );
void stop_leading( monster &z );
void toggle_ignore_targets( monster &z );
void tie_pet( monster &z );
void untie_pet( monster &z );
void shear_animal( monster &z );
void mount_pet( monster &z );
void attach_or_remove_saddle( monster &z );
void deactivate_pet( monster &z );

/*
*Manages the milking and milking cool down of monsters.
*Milked item uses starting_ammo, where ammo type is the milked item
*and amount the times per day you can milk the monster.
*/
void milk_source( monster &source_mon );
} // namespace monexamine

