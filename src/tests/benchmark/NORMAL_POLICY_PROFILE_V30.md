# Fresh normal-policy control profiles

Frozen V30 control, normal Chromium flags, CPU sampling enabled, detailed counters and guest census disabled. Profiles run after the serial grouping screen; they are not speed measurements. No owned build or correctness jobs overlap.

Moving/firing Sky Force's execution worker samples put 25.88% in execute_chain, 11.23% in one generated ARM function and 9.26% in the outer execution loop. Standard Snakes puts 18.22% in execute_chain and 10.45% in validated RAM lookup. Generated functions collectively account for 39.93% / 51.26%. These are weighted worker-span samples, including waits. Inlined calls share their parent's symbol; percentages do not isolate registry lookup or interpreter fallback.

The next hypothesis is a sparse exact ROM lookup index that removes hash lookup and collisions on immutable ROM successors. The profile supports investigating runner cost, but does not predict this change's speedup. Ordinary RAM mapping/lifetime validation and generated block boundaries remain. Focused lifecycle tests and a short exact two-game replay precede a small serial performance screen. Dynamic ROM grouping stays off.

Latest unpaced control measurements remain Sky Force combat 0.528x and Snakes standard 1.600x. Realtime remains unmet.
