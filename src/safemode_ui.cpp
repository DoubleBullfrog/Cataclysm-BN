#include "safemode_ui.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "avatar.h"
#include "cata_utility.h"
#include "color.h"
#include "cursesdef.h"
#include "debug.h"
#include "filesystem.h"
#include "fstream_utils.h"
#include "game.h"
#include "input.h"
#include "json.h"
#include "monstergenerator.h"
#include "mtype.h"
#include "options.h"
#include "output.h"
#include "path_info.h"
#include "point.h"
#include "string_formatter.h"
#include "string_input_popup.h"
#include "string_utils.h"
#include "translations.h"
#include "ui_manager.h"
#include "world.h"

safemode &get_safemode()
{
    static safemode single_instance;
    return single_instance;
}

void safemode::show()
{
    show( _( " SAFE MODE MANAGER " ), true );
}

std::string safemode::npc_type_name()
{
    static std::string name = "human";
    return name;
}

void safemode::show( const std::string &custom_name_in, bool is_safemode_in )
{
    auto global_rules_old = global_rules;
    auto character_rules_old = character_rules;

    const int header_height = 4;
    int content_height = 0;

    enum Columns : int {
        COLUMN_RULE,
        COLUMN_ATTITUDE,
        COLUMN_PROXIMITY,
        COLUMN_WHITE_BLACKLIST,
        COLUMN_CATEGORY
    };

    std::map<int, int> column_pos;
    column_pos[COLUMN_RULE] = 4;
    column_pos[COLUMN_ATTITUDE] = column_pos[COLUMN_RULE] + 38;
    column_pos[COLUMN_PROXIMITY] = column_pos[COLUMN_ATTITUDE] + 10;
    column_pos[COLUMN_WHITE_BLACKLIST] = column_pos[COLUMN_PROXIMITY] + 6;
    column_pos[COLUMN_CATEGORY] = column_pos[COLUMN_WHITE_BLACKLIST] + 11;

    const int num_columns = column_pos.size();

    catacurses::window w_border;
    catacurses::window w_header;
    catacurses::window w;

    ui_adaptor ui;

    const auto init_windows = [&]( ui_adaptor & ui ) {
        content_height = FULL_SCREEN_HEIGHT - 2 - header_height;

        const point offset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                            TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );

        w_border = catacurses::newwin( FULL_SCREEN_HEIGHT, FULL_SCREEN_WIDTH, offset );
        w_header = catacurses::newwin( header_height, FULL_SCREEN_WIDTH - 2,
                                       offset + point_south_east );
        w = catacurses::newwin( content_height, FULL_SCREEN_WIDTH - 2,
                                offset + point( 1, header_height + 1 ) );

        ui.position_from_window( w_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    int tab = GLOBAL_TAB;
    int line = 0;
    int column = 0;
    int start_pos = 0;
    bool changes_made = false;
    input_context ctxt( "SAFEMODE" );
    ctxt.register_cardinal();
    ctxt.register_action( "CONFIRM" );
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "NEXT_TAB" );
    ctxt.register_action( "PREV_TAB" );
    ctxt.register_action( "ADD_DEFAULT_RULESET" );
    ctxt.register_action( "ADD_RULE" );
    ctxt.register_action( "REMOVE_RULE" );
    ctxt.register_action( "COPY_RULE" );
    ctxt.register_action( "ENABLE_RULE" );
    ctxt.register_action( "DISABLE_RULE" );
    ctxt.register_action( "MOVE_RULE_UP" );
    ctxt.register_action( "MOVE_RULE_DOWN" );
    ctxt.register_action( "TEST_RULE" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    if( is_safemode_in ) {
        ctxt.register_action( "SWITCH_SAFEMODE_OPTION" );
        ctxt.register_action( "SWAP_RULE_GLOBAL_CHAR" );
    }

    ui.on_redraw( [&]( const ui_adaptor & ) {
        draw_border( w_border, BORDER_COLOR, custom_name_in );

        mvwputch( w_border, point( 0, 3 ), c_light_gray, LINE_XXXO ); // |-
        mvwputch( w_border, point( 79, 3 ), c_light_gray, LINE_XOXX ); // -|

        for( auto &column : column_pos ) {
            // _|_
            mvwputch( w_border, point( column.second + 1, FULL_SCREEN_HEIGHT - 1 ), c_light_gray, LINE_XXOX );
        }

        wnoutrefresh( w_border );

        static const std::vector<std::string> hotkeys = {{
                translate_marker( "<A>dd" ), translate_marker( "<R>emove" ),
                translate_marker( "<C>opy" ), translate_marker( "<M>ove" ),
                translate_marker( "<E>nable" ), translate_marker( "<D>isable" ),
                translate_marker( "<T>est" )
            }
        };

        int tmpx = 0;
        for( auto &hotkey : hotkeys ) {
            tmpx += shortcut_print( w_header, point( tmpx, 0 ), c_white, c_light_green, _( hotkey ) ) + 2;
        }

        tmpx = 0;
        tmpx += shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green,
                                _( "<+-> Move up/down" ) ) + 2;
        tmpx += shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green,
                                _( "<Enter>-Edit" ) ) + 2;
        shortcut_print( w_header, point( tmpx, 1 ), c_white, c_light_green, _( "<Tab>-Switch Page" ) );

        for( int i = 0; i < 78; i++ ) {
            mvwputch( w_header, point( i, 2 ), c_light_gray, LINE_OXOX ); // Draw line under header
        }

        for( auto &pos : column_pos ) {
            mvwputch( w_header, point( pos.second, 2 ), c_light_gray, LINE_OXXX );
            mvwputch( w_header, point( pos.second, 3 ), c_light_gray, LINE_XOXO );
        }

        mvwprintz( w_header, point( 1, 3 ), c_white, "#" );
        mvwprintz( w_header, point( column_pos[COLUMN_RULE] + 4, 3 ), c_white, _( "Rules" ) );
        mvwprintz( w_header, point( column_pos[COLUMN_ATTITUDE] + 2, 3 ), c_white, _( "Attitude" ) );
        mvwprintz( w_header, point( column_pos[COLUMN_PROXIMITY] + 2, 3 ), c_white, _( "Dist" ) );
        mvwprintz( w_header, point( column_pos[COLUMN_WHITE_BLACKLIST] + 2, 3 ), c_white, _( "B/W" ) );
        mvwprintz( w_header, point( column_pos[COLUMN_CATEGORY] + 2, 3 ), c_white, pgettext( "category",
                   "Cat" ) );

        int locx = 17;
        locx += shortcut_print( w_header, point( locx, 2 ), c_white,
                                ( tab == GLOBAL_TAB ) ? hilite( c_white ) : c_white, _( "[<Global>]" ) ) + 1;
        shortcut_print( w_header, point( locx, 2 ), c_white,
                        ( tab == CHARACTER_TAB ) ? hilite( c_white ) : c_white, _( "[<Character>]" ) );

        locx = 55;
        mvwprintz( w_header, point( locx, 0 ), c_white, _( "Safe Mode enabled:" ) );
        locx += shortcut_print( w_header, point( locx, 1 ),
                                ( ( get_option<bool>( "SAFEMODE" ) ) ? c_light_green : c_light_red ), c_white,
                                ( ( get_option<bool>( "SAFEMODE" ) ) ? _( "True" ) : _( "False" ) ) );
        locx += shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, "  " );
        locx += shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, _( "<S>witch" ) );
        shortcut_print( w_header, point( locx, 1 ), c_white, c_light_green, "  " );

        wnoutrefresh( w_header );

        // Clear the lines
        for( int i = 0; i < content_height; i++ ) {
            for( int j = 0; j < 79; j++ ) {
                mvwputch( w, point( j, i ), c_black, ' ' );
            }

            for( auto &pos : column_pos ) {
                mvwputch( w, point( pos.second, i ), c_light_gray, LINE_XOXO );
            }
        }

        auto &current_tab = ( tab == GLOBAL_TAB ) ? global_rules : character_rules;

        if( tab == CHARACTER_TAB && g->u.name.empty() ) {
            character_rules.clear();
            mvwprintz( w, point( 15, 8 ), c_white, _( "Please load a character first to use this page!" ) );
        } else if( empty() ) {
            mvwprintz( w, point( 15, 8 ), c_white, _( "Safe Mode manager currently inactive." ) );
            mvwprintz( w, point( 15, 9 ), c_white, _( "Default rules are used.  Add a rule to activate." ) );
            mvwprintz( w, point( 15, 10 ), c_white, _( "Press ~ to add a default ruleset to get started." ) );
        }

        draw_scrollbar( w_border, line, content_height, current_tab.size(), point( 0, 5 ) );
        wnoutrefresh( w_border );

        calcStartPos( start_pos, line, content_height, current_tab.size() );

        // display safe mode
        for( int i = start_pos; i < static_cast<int>( current_tab.size() ); i++ ) {
            if( i >= start_pos &&
                i < start_pos + std::min( content_height, static_cast<int>( current_tab.size() ) ) ) {

                auto rule = current_tab[i];

                nc_color line_color = ( rule.active ) ? c_white : c_light_gray;

                mvwprintz( w, point( 1, i - start_pos ), line_color, "%d", i + 1 );
                mvwprintz( w, point( 5, i - start_pos ), c_yellow, ( line == i ) ? ">> " : "   " );

                auto draw_column = [&]( Columns column_in, const std::string & text_in ) {
                    mvwprintz( w, point( column_pos[column_in] + 2, i - start_pos ),
                               ( line == i && column == column_in ) ? hilite( line_color ) : line_color,
                               text_in
                             );
                };

                draw_column( COLUMN_RULE, ( rule.rule.empty() ) ? _( "<empty rule>" ) : rule.rule );
                draw_column( COLUMN_ATTITUDE, ( rule.category == Categories::HOSTILE_SPOTTED ) ?
                             Creature::get_attitude_ui_data( rule.attitude ).first.translated() : "---" );
                draw_column( COLUMN_PROXIMITY, ( ( rule.category == Categories::SOUND ) ||
                                                 !rule.whitelist ) ? std::to_string( rule.proximity ) : "---" );
                draw_column( COLUMN_WHITE_BLACKLIST, rule.whitelist ? _( "Whitelist" ) : _( "Blacklist" ) );
                draw_column( COLUMN_CATEGORY, ( rule.category == Categories::SOUND ) ? _( "Sound" ) :
                             _( "Hostile" ) );
            }
        }

        wnoutrefresh( w );
    } );

    while( true ) {
        auto &current_tab = ( tab == GLOBAL_TAB ) ? global_rules : character_rules;

        ui_manager::redraw();

        const std::string action = ctxt.handle_input();

        if( action == "NEXT_TAB" ) {
            tab++;
            if( tab >= MAX_TAB ) {
                tab = 0;
            }
            line = 0;
        } else if( action == "PREV_TAB" ) {
            tab--;
            if( tab < 0 ) {
                tab = MAX_TAB - 1;
            }
            line = 0;
        } else if( action == "QUIT" ) {
            break;
        } else if( tab == CHARACTER_TAB && g->u.name.empty() ) {
            //Only allow loaded games to use the char sheet
        } else if( action == "DOWN" ) {
            line++;
            if( line >= static_cast<int>( current_tab.size() ) ) {
                line = 0;
            }
        } else if( action == "UP" ) {
            line--;
            if( line < 0 ) {
                line = current_tab.size() - 1;
            }
        } else if( action == "ADD_DEFAULT_RULESET" ) {
            changes_made = true;
            current_tab.emplace_back( "*", true, false, Attitude::A_HOSTILE,
                                      get_option<int>( "SAFEMODEPROXIMITY" )
                                      , HOSTILE_SPOTTED );
            current_tab.emplace_back( "*", true, true, Attitude::A_HOSTILE, 5, SOUND );
            line = current_tab.size() - 1;
        } else if( action == "ADD_RULE" ) {
            changes_made = true;
            current_tab.emplace_back( "", true, false, Attitude::A_HOSTILE,
                                      get_option<int>( "SAFEMODEPROXIMITY" ), HOSTILE_SPOTTED );
            line = current_tab.size() - 1;
        } else if( action == "REMOVE_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab.erase( current_tab.begin() + line );
            if( line > static_cast<int>( current_tab.size() ) - 1 ) {
                line--;
            }
            if( line < 0 ) {
                line = 0;
            }
        } else if( action == "COPY_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab.push_back( current_tab[line] );
            line = current_tab.size() - 1;
        } else if( action == "SWAP_RULE_GLOBAL_CHAR" && !current_tab.empty() ) {
            if( ( tab == GLOBAL_TAB && !g->u.name.empty() ) || tab == CHARACTER_TAB ) {
                changes_made = true;
                //copy over
                auto &temp_rules_from = ( tab == GLOBAL_TAB ) ? global_rules : character_rules;
                auto &temp_rules_to = ( tab == GLOBAL_TAB ) ? character_rules : global_rules;

                temp_rules_to.push_back( temp_rules_from[line] );

                //remove old
                temp_rules_from.erase( temp_rules_from.begin() + line );
                line = temp_rules_to.size() - 1;
                tab = ( tab == GLOBAL_TAB ) ? CHARACTER_TAB : GLOBAL_TAB;
            }
        } else if( action == "CONFIRM" && !current_tab.empty() ) {
            changes_made = true;
            if( column == COLUMN_RULE ) {
                catacurses::window w_help;
                ui_adaptor help_ui;
                const auto init_help_window = [&]( ui_adaptor & help_ui ) {
                    const point offset( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0,
                                        TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 : 0 );

                    w_help = catacurses::newwin( FULL_SCREEN_HEIGHT / 2 - 2, FULL_SCREEN_WIDTH * 3 / 4,
                                                 offset + point( 19 / 2, 7 + FULL_SCREEN_HEIGHT / 2 / 2 ) );

                    help_ui.position_from_window( w_help );
                };
                init_help_window( help_ui );
                help_ui.on_screen_resize( init_help_window );

                help_ui.on_redraw( [&]( const ui_adaptor & ) {
                    switch( current_tab[line].category ) {
                        case Categories::HOSTILE_SPOTTED:
                            // NOLINTNEXTLINE(cata-use-named-point-constants)
                            fold_and_print( w_help, point( 1, 1 ), 999, c_white,
                                            _(
                                                "* is used as a Wildcard.  A few Examples:\n"
                                                "\n"
                                                "human          matches every NPC\n"
                                                "zombie         matches the monster name exactly\n"
                                                "acidic zo*     matches monsters beginning with 'acidic zo'\n"
                                                "*mbie          matches monsters ending with 'mbie'\n"
                                                "*cid*zo*ie     multiple * are allowed\n"
                                                "AcI*zO*iE      case insensitive search" )
                                          );
                            break;
                        case Categories::SOUND:
                            // NOLINTNEXTLINE(cata-use-named-point-constants)
                            fold_and_print( w_help, point( 1, 1 ), 999, c_white,
                                            _(
                                                "* is used as a Wildcard.  A few Examples:\n"
                                                "\n"
                                                "footsteps      matches the sound name exactly\n"
                                                "a loud ba*     matches sounds beginning with 'a loud ba'\n"
                                                "*losion!       matches sounds ending with 'losion!'\n"
                                                "a *oud*ba*     multiple * are allowed\n"
                                                "*LoU*bA*       case insensitive search" )
                                          );
                            break;
                        default:
                            break;
                    }
                    draw_border( w_help );
                    wnoutrefresh( w_help );
                } );

                current_tab[line].rule = wildcard_trim_rule( string_input_popup()
                                         .title( _( "Safe Mode Rule:" ) )
                                         .width( 30 )
                                         .text( current_tab[line].rule )
                                         .query_string() );
            } else if( column == COLUMN_WHITE_BLACKLIST ) {
                current_tab[line].whitelist = !current_tab[line].whitelist;
            } else if( column == COLUMN_CATEGORY ) {
                if( current_tab[line].category == HOSTILE_SPOTTED ) {
                    current_tab[line].category = SOUND;
                } else if( current_tab[line].category == SOUND ) {
                    current_tab[line].category = HOSTILE_SPOTTED;
                }
            } else if( column == COLUMN_ATTITUDE ) {
                auto &attitude = current_tab[line].attitude;
                switch( attitude ) {
                    case Attitude::A_HOSTILE:
                        attitude = Attitude::A_NEUTRAL;
                        break;
                    case Attitude::A_NEUTRAL:
                        attitude = Attitude::A_FRIENDLY;
                        break;
                    case Attitude::A_FRIENDLY:
                        attitude = Attitude::A_ANY;
                        break;
                    case Attitude::A_ANY:
                        attitude = Attitude::A_HOSTILE;
                        break;
                    case Attitude::NUM_A:
                        attitude = Attitude::A_NEUTRAL;
                }
            } else if( column == COLUMN_PROXIMITY && ( current_tab[line].category == SOUND ||
                       !current_tab[line].whitelist ) ) {
                const auto text = string_input_popup()
                                  .title( _( "Proximity Distance (0=max view distance)" ) )
                                  .width( 4 )
                                  .text( std::to_string( current_tab[line].proximity ) )
                                  .description( _( "Option: " ) + std::to_string( get_option<int>( "SAFEMODEPROXIMITY" ) ) +
                                                " " + get_options().get_option( "SAFEMODEPROXIMITY" ).getDefaultText() )
                                  .max_length( 3 )
                                  .only_digits( true )
                                  .query_string();
                if( text.empty() ) {
                    current_tab[line].proximity = get_option<int>( "SAFEMODEPROXIMITY" );
                } else {
                    //Let the options class handle the validity of the new value
                    auto temp_option = get_options().get_option( "SAFEMODEPROXIMITY" );
                    temp_option.setValue( text );
                    current_tab[line].proximity = atoi( temp_option.getValue().c_str() );
                }
            }
        } else if( action == "ENABLE_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab[line].active = true;
        } else if( action == "DISABLE_RULE" && !current_tab.empty() ) {
            changes_made = true;
            current_tab[line].active = false;
        } else if( action == "LEFT" ) {
            column--;
            if( column < 0 ) {
                column = num_columns - 1;
            }
        } else if( action == "RIGHT" ) {
            column++;
            if( column >= num_columns ) {
                column = 0;
            }
        } else if( action == "MOVE_RULE_UP" && !current_tab.empty() ) {
            changes_made = true;
            if( line < static_cast<int>( current_tab.size() ) - 1 ) {
                std::swap( current_tab[line], current_tab[line + 1] );
                line++;
                column = 0;
            }
        } else if( action == "MOVE_RULE_DOWN" && !current_tab.empty() ) {
            changes_made = true;
            if( line > 0 ) {
                std::swap( current_tab[line],  current_tab[line - 1] );
                line--;
                column = 0;
            }
        } else if( action == "TEST_RULE" && !current_tab.empty() ) {
            test_pattern( tab, line );
        } else if( action == "SWITCH_SAFEMODE_OPTION" ) {
            get_options().get_option( "SAFEMODE" ).setNext();
            get_options().save();
        }
    }

    if( !changes_made ) {
        return;
    }

    if( query_yn( _( "Save changes?" ) ) ) {
        if( is_safemode_in ) {
            save_global();
            if( !g->u.name.empty() ) {
                save_character();
            }
        } else {
            create_rules();
        }
    } else {
        global_rules = global_rules_old;
        character_rules = character_rules_old;
    }
}

void safemode::test_pattern( const int tab_in, const int row_in )
{
    std::vector<std::string> creature_list;

    auto &temp_rules = ( tab_in == GLOBAL_TAB ) ? global_rules : character_rules;

    if( temp_rules[row_in].rule.empty() ) {
        return;
    }

    if( g->u.name.empty() ) {
        popup( _( "No monsters loaded.  Please start a game first." ) );
        return;
    }

    //Loop through all monster mtypes
    for( const auto &mtype : MonsterGenerator::generator().get_all_mtypes() ) {
        std::string creature_name = mtype.nname();
        if( wildcard_match( creature_name, temp_rules[row_in].rule ) ) {
            creature_list.push_back( creature_name );
        }
    }

    int start_pos = 0;
    int content_height = 0;
    int content_width = 0;

    catacurses::window w_test_rule_border;
    catacurses::window w_test_rule_content;

    ui_adaptor ui;
    const auto init_windows = [&]( ui_adaptor & ui ) {
        const point offset( 15 + ( TERMX > FULL_SCREEN_WIDTH ? ( TERMX - FULL_SCREEN_WIDTH ) / 2 : 0 ),
                            5 + ( TERMY > FULL_SCREEN_HEIGHT ? ( TERMY - FULL_SCREEN_HEIGHT ) / 2 :
                                  0 ) );

        content_height = FULL_SCREEN_HEIGHT - 8;
        content_width = FULL_SCREEN_WIDTH - 30;

        w_test_rule_border = catacurses::newwin( content_height + 2, content_width,
                             offset );
        w_test_rule_content = catacurses::newwin( content_height, content_width - 2,
                              offset + point_south_east );

        ui.position_from_window( w_test_rule_border );
    };
    init_windows( ui );
    ui.on_screen_resize( init_windows );

    int nmatch = creature_list.size();
    const std::string buf = string_format( vgettext( "%1$d monster matches: %2$s",
                                           "%1$d monsters match: %2$s",
                                           nmatch ), nmatch, temp_rules[row_in].rule.c_str() );

    int line = 0;

    input_context ctxt( "SAFEMODE_TEST" );
    ctxt.register_updown();
    ctxt.register_action( "QUIT" );
    ctxt.register_action( "HELP_KEYBINDINGS" );

    ui.on_redraw( [&]( const ui_adaptor & ) {
        draw_border( w_test_rule_border, BORDER_COLOR, buf, hilite( c_white ) );
        center_print( w_test_rule_border, content_height + 1, red_background( c_white ),
                      _( "Lists monsters regardless of their attitude." ) );

        wnoutrefresh( w_test_rule_border );

        // Clear the lines
        for( int i = 0; i < content_height; i++ ) {
            for( int j = 0; j < 79; j++ ) {
                mvwputch( w_test_rule_content, point( j, i ), c_black, ' ' );
            }
        }

        calcStartPos( start_pos, line, content_height, creature_list.size() );

        // display safe mode
        for( int i = start_pos; i < static_cast<int>( creature_list.size() ); i++ ) {
            if( i >= start_pos &&
                i < start_pos + std::min( content_height, static_cast<int>( creature_list.size() ) ) ) {
                nc_color line_color = c_white;

                mvwprintz( w_test_rule_content, point( 0, i - start_pos ), line_color, "%d", i + 1 );
                mvwprintz( w_test_rule_content, point( 4, i - start_pos ), line_color, "" );

                wprintz( w_test_rule_content, c_yellow, ( line == i ) ? ">> " : "   " );

                wprintz( w_test_rule_content, ( line == i ) ? hilite( line_color ) : line_color,
                         creature_list[i] );
            }
        }

        wnoutrefresh( w_test_rule_content );
    } );

    while( true ) {
        ui_manager::redraw();

        const std::string action = ctxt.handle_input();
        if( action == "DOWN" ) {
            line++;
            if( line >= static_cast<int>( creature_list.size() ) ) {
                line = 0;
            }
        } else if( action == "UP" ) {
            line--;
            if( line < 0 ) {
                line = creature_list.size() - 1;
            }
        } else if( action == "QUIT" ) {
            break;
        }
    }
}

void safemode::add_rule( const std::string &rule_in, const Attitude attitude_in,
                         const int proximity_in,
                         const rule_state state_in )
{
    character_rules.emplace_back( rule_in, true, ( state_in == RULE_WHITELISTED ),
                                  attitude_in, proximity_in, HOSTILE_SPOTTED );
    create_rules();

    if( !get_option<bool>( "SAFEMODE" ) &&
        query_yn( _( "Safe Mode is not enabled in the options.  Enable it now?" ) ) ) {
        get_options().get_option( "SAFEMODE" ).setNext();
        get_options().save();
    }
}

bool safemode::has_rule( const std::string &rule_in, const Attitude attitude_in )
{
    for( auto &elem : character_rules ) {
        if( rule_in.length() == elem.rule.length()
            && ci_find_substr( rule_in, elem.rule ) != -1
            && elem.attitude == attitude_in ) {
            return true;
        }
    }
    return false;
}

void safemode::remove_rule( const std::string &rule_in, const Attitude attitude_in )
{
    for( auto it = character_rules.begin();
         it != character_rules.end(); ++it ) {
        if( rule_in.length() == it->rule.length()
            && ci_find_substr( rule_in, it->rule ) != -1
            && it->attitude == attitude_in ) {
            character_rules.erase( it );
            create_rules();
            break;
        }
    }
}

bool safemode::empty() const
{
    return global_rules.empty() && character_rules.empty();
}

void safemode::create_rules()
{
    safemode_rules_hostile.clear();
    safemode_rules_sound.clear();
    //process include/exclude in order of rules, global first, then character specific
    add_rules( global_rules );
    add_rules( character_rules );
}

void safemode::add_rules( const std::vector<rules_class> &rules_in )
{
    //if a specific monster is being added, all the rules need to be checked now
    //may have some performance issues since exclusion needs to check all monsters also
    for( auto &rule : rules_in ) {
        switch( rule.category ) {
            case HOSTILE_SPOTTED:
                if( !rule.whitelist ) {
                    //Check include patterns against all monster mtypes
                    for( const auto &mtype : MonsterGenerator::generator().get_all_mtypes() ) {
                        set_rule( rule, mtype.nname(), RULE_BLACKLISTED );
                    }
                } else {
                    //exclude monsters from the existing mapping
                    for( const auto &safemode_rule : safemode_rules_hostile ) {
                        set_rule( rule, safemode_rule.first, RULE_WHITELISTED );
                    }
                }
                break;
            case SOUND:
                set_rule( rule, rule.rule, rule.whitelist ? RULE_WHITELISTED : RULE_BLACKLISTED );
                break;
            default:
                break;
        }
    }
}

void safemode::set_rule( const rules_class &rule_in, const std::string &name_in, rule_state rs_in )
{
    static std::vector<Attitude> attitude_any = { {Attitude::A_HOSTILE, Attitude::A_NEUTRAL, Attitude::A_FRIENDLY} };
    switch( rule_in.category ) {
        case HOSTILE_SPOTTED:
            if( !rule_in.rule.empty() && rule_in.active && wildcard_match( name_in, rule_in.rule ) ) {
                if( rule_in.attitude == Attitude::A_ANY ) {
                    for( auto &att : attitude_any ) {
                        safemode_rules_hostile[name_in][att] = rule_state_class( rs_in, rule_in.proximity,
                                                               HOSTILE_SPOTTED );
                    }
                } else {
                    safemode_rules_hostile[name_in][rule_in.attitude] = rule_state_class( rs_in, rule_in.proximity,
                            HOSTILE_SPOTTED );
                }
            }
            break;
        case SOUND:
            safemode_rules_sound.push_back( rule_in );
            break;
        default:
            break;
    }
}

rule_state safemode::check_monster( const std::string &creature_name_in,
                                    const Attitude attitude_in,
                                    const int proximity_in ) const
{
    const auto iter = safemode_rules_hostile.find( creature_name_in );
    if( iter != safemode_rules_hostile.end() ) {
        const auto &tmp = ( iter->second )[static_cast<int>( attitude_in )];
        if( tmp.state == RULE_BLACKLISTED ) {
            if( tmp.proximity == 0 || proximity_in <= tmp.proximity ) {
                return RULE_BLACKLISTED;
            }

        } else if( tmp.state == RULE_WHITELISTED ) {
            return RULE_WHITELISTED;
        }
    }

    return RULE_NONE;
}

bool safemode::is_sound_safe( const std::string &sound_name_in,
                              const int proximity_in ) const
{
    bool sound_safe = false;
    for( const rules_class &rule : safemode_rules_sound ) {
        if( wildcard_match( sound_name_in, rule.rule ) &&
            proximity_in >= rule.proximity ) {
            if( rule.whitelist ) {
                sound_safe = true;
            } else {
                return false;
            }
        }
    }
    return sound_safe;
}

void safemode::clear_character_rules()
{
    character_rules.clear();
}

bool safemode::save_character()
{
    return save( true );
}

bool safemode::save_global()
{
    return save( false );
}

bool safemode::save( const bool is_character_in )
{
    is_character = is_character_in;
    auto serializer = [&]( std::ostream & fout ) {
        JsonOut jout( fout, true );
        serialize( jout );

        if( !is_character ) {
            create_rules();
        }
    };

    if( is_character ) {
        world *world = g->get_active_world();
        if( !world->player_file_exist( ".sav" ) ) {
            return true; //Character not saved yet.
        }

        return world->write_to_player_file( ".sfm.json", serializer, _( "safemode configuration" ) );
    } else {
        return write_to_file( PATH_INFO::safemode(), serializer, _( "safemode configuration" ) );
    }
}

void safemode::load_character()
{
    load( true );
}

void safemode::load_global()
{
    load( false );
}

void safemode::load( const bool is_character_in )
{
    is_character = is_character_in;
    auto loader = [&]( JsonIn & jsin ) {
        try {
            deserialize( jsin );
        } catch( const JsonError &e ) {
            debugmsg( "Error loading safemode settings: %s", e.c_str() );
        }
    };

    std::ifstream fin;
    if( is_character ) {
        g->get_active_world()->read_from_player_file_json( ".sfm.json", loader, true );
    } else {
        read_from_file_json( PATH_INFO::safemode(), loader, true );
    }

    create_rules();
}

void safemode::serialize( JsonOut &json ) const
{
    json.start_array();

    auto &temp_rules = ( is_character ) ? character_rules : global_rules;
    for( auto &elem : temp_rules ) {
        json.start_object();

        json.member( "rule", elem.rule );
        json.member( "active", elem.active );
        json.member( "whitelist", elem.whitelist );
        json.member( "attitude", elem.attitude );
        json.member( "proximity", elem.proximity );
        json.member( "category", elem.category );

        json.end_object();
    }

    json.end_array();
}

void safemode::deserialize( JsonIn &jsin )
{
    auto &temp_rules = ( is_character ) ? character_rules : global_rules;
    temp_rules.clear();

    jsin.start_array();
    while( !jsin.end_array() ) {
        JsonObject jo = jsin.get_object();

        const std::string rule = jo.get_string( "rule" );
        const bool active = jo.get_bool( "active" );
        const bool whitelist = jo.get_bool( "whitelist" );
        const Attitude attitude = static_cast<Attitude>( jo.get_int( "attitude" ) );
        const int proximity = jo.get_int( "proximity" );
        const Categories cat = jo.has_member( "category" ) ? static_cast<Categories>
                               ( jo.get_int( "category" ) ) : HOSTILE_SPOTTED;

        temp_rules.emplace_back( rule, active, whitelist, attitude, proximity, cat
                               );
    }
}
