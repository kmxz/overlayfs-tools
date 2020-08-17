TESTS := diff verbose brief

all: $(TESTS)

overlayed: overlayed.tar
	sudo tar xf $<
	# "fake" that lower removed_dir was actually renamed and not removed
	sudo cp -a permanent/removed_tests/removed_dir overlayed/renamed_dir

permanent: lower.tar
	sudo tar xf $<

changes: upper.tar
	sudo tar xf $< --strip-components=2
	sudo setfattr -n trusted.overlay.opaque -v y changes/removed_tests/replaced_by_opaque
	sudo setfattr -n trusted.overlay.opaque -v y changes/normal_merged_dir/more_test/by_opaque
	# Only permission bits changed - make upper a "fake" emulated metacopy
	# "fake" because it is missing trusted.overlay.origin xattr, so won't be
	# accessible in real overlay mount
	sudo setfattr -n trusted.overlay.metacopy -v y changes/permission/file
	sudo truncate -s 0 changes/permission/file
	sudo truncate -s 95 changes/permission/file
	# "fake" that lower removed_dir was actually renamed and not removed,
	# so all its children can be found under renamed_dir
	sudo mkdir changes/renamed_dir
	sudo setfattr -n trusted.overlay.redirect -v '/removed_tests/removed_dir' changes/renamed_dir

OVERLAY := ../overlay

brief.expected: permanent overlayed
	diff -qr --no-dereference $(CURDIR)/permanent $(CURDIR)/overlayed 2>/dev/null | sed 's/overlayed/changes/g' | sort -u > $@

diff.out: $(OVERLAY) permanent changes
	sudo $(OVERLAY) -l permanent/ -u changes/ diff | sort -u > $@

verbose.out: $(OVERLAY) permanent changes
	sudo $(OVERLAY) -l permanent -u changes diff -v | sort -u > $@

brief.out: $(OVERLAY) permanent changes
	sudo $(OVERLAY) -l permanent -u changes diff -b | sort -u > $@

# diff.saved changes should be committed if diff.out changes intentionally
diff: diff.saved diff.out
	diff -u $^

# verbose.saved changes should be committed if verbose.out changes intentionally
verbose: verbose.saved verbose.out
	diff -u $^

brief: brief.expected brief.out
	diff -u $^

clean:
	sudo rm -rf permanent changes overlayed *.expected *.out
