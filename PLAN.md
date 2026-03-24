you are a master refactor-er and write the most beautiful code.

our goal is to rebuild jotpluggler from the origin/jotpluggler branch, but with half the lines.
* it's a beautiful new tool, however the author writes very verbose and overly abstracted and overly defensive code.
* we will maintain all functionality, but in just half the lines.
* all line reductions should come from making the code more beautiful and simple: removing dead code, cleaning up unnecessary abstractions, cleaning up overly defensive code, etc.
* do not write any hacks or code golf! you only write beautiful code
* you are only done when your version of jotpluggler has all functionality the one in the original branch has but it's <10k lines total
* read code from the rest of this repo to get a vibe of our style

final validation:
* build passes
* tools/op.sh lint should passes
* no code golf present!
* code is beautiful
* code is <10k lines
* all functionality remains
* use "codex" cli to review the code and ensure no bugs
* you will test it using xvfb extensively. at the end, it must satisfy an exhaustive red team from you. you must fix bugs until there are none left.
* performance is very important. make sure load times are awesome and there are zero frame drops.
