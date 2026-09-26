/* Build-time helper for web/mktiles.py: prints the game's display symbols
 * (nh_get_drawing_info() names, plus object name/description/class and
 * monster class) one per line: kind \t index \t name [\t desc \t class]. */
#include "hack.h"

void *xmalloc(int n) { return malloc(n); }

static void list(const char *k, const struct nh_symdef *s, int n)
{
    int i;

    for (i = 0; i < n; i++)
        printf("%s\t%d\t%s\n", k, i, s[i].symname);
}

int main(void)
{
    struct nh_drawing_info *d = nh_get_drawing_info();
    int i;

    list("bg", d->bgelements, d->num_bgelements);
    list("trap", d->traps, d->num_traps);
    for (i = 0; i < d->num_objects; i++)
        printf("obj\t%d\t%s\t%s\t%d\n", i, obj_descr[i].oc_name ? obj_descr[i].oc_name : "",
               obj_descr[i].oc_descr ? obj_descr[i].oc_descr : "", const_objects[i].oc_class);
    for (i = 0; i < d->num_monsters; i++)
        printf("mon\t%d\t%s\t\t%d\n", i, d->monsters[i].symname, mons[i].mlet);
    list("warn", d->warnings, d->num_warnings);
    list("expl", d->expltypes, d->num_expltypes);
    list("zap", d->zaptypes, d->num_zaptypes);
    list("effect", d->effects, d->num_effects);
    return 0;
}
