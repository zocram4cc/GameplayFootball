"""Refreshes a team's player stats in place, leaving every row where it is.

    python3 refresh_stats.py <database.sqlite> <team.ted> [<team.ted> ...]

install_team.install() rewrites a team by deleting its roster and inserting it
again, so every player lands on a new row id. That is right for a first import
and ruinous for a stats fix: playermodels.cfg and playerportraits.cfg bind by
row id, and a hand-tuned binding (a pack no longer on disk, models bound in
export order) is gone the moment the ids churn. The seven 4cc keepers ran on
outfield fallbacks for four days because gk_* joined STAT_KEYS after the
database was last generated, and the only regeneration on offer cost the
models.

This writes base_stat and profile_xml - what a player IS - and nothing else,
matched by (team, formationorder): the same slot install() keys the row on.
It refuses rather than guesses when the export does not line up with the
roster in the database, name for name, slot for slot. The conversion is
install_team.stat_profile_xml, so the result is byte-identical to what a fresh
install would have written for the same record.
"""

import argparse
import sqlite3
import sys

import install_team
import ted


def team_row(cur, name):
    """The team's row id by name, tolerant of the slashes a board name wears."""
    for candidate in (name, name.strip("/"), "/%s/" % name.strip("/")):
        row = cur.execute("select id from teams where lower(name) = lower(?)",
                          (candidate,)).fetchone()
        if row:
            return row[0]
    return None


def refresh(database, team, dry_run=False):
    """Rewrites base_stat and profile_xml for every slot of `team`.

    -> the number of rows written. Raises ValueError when the team is not in
    the database or its roster does not match the export slot for slot.
    """
    name = team.get("team") or team.get("name")
    if not name:
        raise ValueError("export carries no team name")
    players = team.get("players") or []
    if not players:
        raise ValueError("%s has no players to refresh" % name)

    conn = sqlite3.connect(database)
    try:
        cur = conn.cursor()
        tid = team_row(cur, name)
        if tid is None:
            raise ValueError("%s is not in the database; use import_team.py for a first install"
                             % name)
        rows = cur.execute("select id, formationorder, role, lastname from players "
                           "where team_id = ? order by formationorder", (tid,)).fetchall()
        if len(rows) != len(players):
            raise ValueError("%s: database has %d players, export has %d"
                             % (name, len(rows), len(players)))
        mismatches = [(slot, stored, players[slot]["name"][:64])
                      for _, slot, _, stored in rows
                      if stored != players[slot]["name"][:64]]
        if mismatches:
            raise ValueError("%s: roster does not match the export by slot: %r"
                             % (name, mismatches[:5]))

        written = 0
        for row_id, slot, role, _ in rows:
            player = players[slot]
            stats = player.get("stats")
            if stats:
                base_stat = install_team.pes_to_base(sum(stats.values()) / len(stats))
                profile = install_team.stat_profile_xml(
                    stats, role, slot, player.get("abilities"), player.get("playing_style"),
                    player.get("com_styles"), player.get("skills"))
            else:
                base_stat = install_team.BASE_STAT
                profile = install_team.profile_xml(install_team.BASE_STAT, role, slot)
            cur.execute("update players set base_stat = ?, profile_xml = ? where id = ?",
                        (base_stat, profile, row_id))
            written += 1
        if dry_run:
            conn.rollback()
        else:
            conn.commit()
        return written
    finally:
        conn.close()


def reseat(database, team, dry_run=False):
    """Seats an installed squad on PES's own team sheet, without moving a row.

    `refresh` rewrites what a player IS; this rewrites WHERE HE PLAYS -
    formationorder, the role that seat carries, and the profile that role
    generates. The seven 4cc teams were installed under the old list-order
    rule and are all seated wrong; re-importing would fix the order and move
    every row id, unbinding playermodels.cfg. Players are matched by name, so
    the rows stay exactly where the model bindings expect them.

    -> the number of rows reseated. Raises ValueError when the team is absent,
    the export carries no usable sheet, or the rosters do not match by name.
    """
    name = team.get("team") or team.get("name")
    if not name:
        raise ValueError("export carries no team name")
    players = team.get("players") or []
    squad = team.get("squad") or []
    if not players or not squad:
        raise ValueError("%s has no squad to reseat" % name)
    sheet = install_team.starting_slots(team.get("squad_order") or [], len(squad))
    if not sheet:
        raise ValueError("%s carries no usable team sheet (squad_order)" % name)
    # Seat 0 belongs to the keeper, always. PES's slot is the sheet's own
    # ORDER, not a position - the keeper sits at slot 1 on /lcg/, /hdg/ and
    # /vn/, at 9 on /2hug/ and /smbg/, at 10 on /dbg/ - and the engine needs
    # exactly one keeper in the first seat, so he is pinned there (the export's
    # first player, shirt 1, as the importer has always taken him) and the
    # sheet fills the outfield ten in its own order.
    keeper = 0
    listed = list(range(len(squad)))
    outfield = sorted((i for i in listed if i in sheet and i != keeper),
                      key=lambda i: sheet[i])
    seated = [keeper] + outfield[:install_team.STARTING_SLOTS - 1]
    order = seated + [i for i in listed if i not in seated]

    conn = sqlite3.connect(database)
    try:
        cur = conn.cursor()
        tid = team_row(cur, name)
        if tid is None:
            raise ValueError("%s is not in the database" % name)
        rows = cur.execute("select id, lastname from players where team_id = ?",
                           (tid,)).fetchall()
        by_name = {}
        for row_id, stored in rows:
            by_name.setdefault(stored, []).append(row_id)
        wanted = [players[i]["name"][:64] for i in order if i < len(players)]
        missing = [n for n in wanted if not by_name.get(n)]
        if len(rows) != len(wanted) or missing:
            raise ValueError("%s: roster does not match the export by name (%d rows, %d in "
                             "export, missing %r)" % (name, len(rows), len(wanted), missing[:5]))

        written = 0
        for slot, source in enumerate(order):
            if source >= len(players):
                continue
            player = players[source]
            row_id = by_name[player["name"][:64]].pop(0)
            role = (install_team.KEEPER_ROLE if slot == 0
                    else install_team.FIELD_ROLES[(slot - 1) % len(install_team.FIELD_ROLES)])
            stats = player.get("stats")
            if stats:
                base_stat = install_team.pes_to_base(sum(stats.values()) / len(stats))
                profile = install_team.stat_profile_xml(
                    stats, role, slot, player.get("abilities"), player.get("playing_style"),
                    player.get("com_styles"), player.get("skills"))
            else:
                base_stat = install_team.BASE_STAT
                profile = install_team.profile_xml(install_team.BASE_STAT, role, slot)
            cur.execute("update players set formationorder = ?, role = ?, base_stat = ?, "
                        "profile_xml = ? where id = ?",
                        (slot, role, base_stat, profile, row_id))
            written += 1
        if dry_run:
            conn.rollback()
        else:
            conn.commit()
        return written
    finally:
        conn.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("database")
    parser.add_argument("teds", nargs="+", metavar="team.ted")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would be written, write nothing")
    parser.add_argument("--reseat", action="store_true",
                        help="also seat the squad on PES's team sheet (formationorder "
                             "and the role each seat carries), matching players by name "
                             "so row ids - and the model bindings keyed on them - hold")
    args = parser.parse_args()
    failed = False
    for path in args.teds:
        team, _ = ted.read_export(path)
        action = reseat if args.reseat else refresh
        verb = "reseated" if args.reseat else "refreshed"
        try:
            n = action(args.database, team, dry_run=args.dry_run)
            print("%s: %d players %s%s" % (team["team"], n, verb,
                                           " (dry run)" if args.dry_run else ""))
        except ValueError as err:
            failed = True
            print("%s: REFUSED - %s" % (path, err), file=sys.stderr)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
