<script lang="ts">
	import { createEventDispatcher } from 'svelte';
	export let command;
	const dispatch = createEventDispatcher();
</script>

<section>
	<fieldset disabled={command.skip}>
		<label>
			<input type="checkbox" bind:checked={command.is_stop_pkt} />
			stop packet
		</label>
		<label>
			<input type="number" bind:value={command.r} max={999} />
			repeat
		</label>

		<fieldset disabled={command.is_stop_pkt}>
			<label>
				<input type="checkbox" bind:checked={command.lf} />
				left-forward
			</label>
			<label>
				<input type="checkbox" bind:checked={command.lb} />
				left-backward
			</label>
			<label>
				<input type="checkbox" bind:checked={command.rf} />
				right-forward
			</label>
			<label>
				<input type="checkbox" bind:checked={command.rb} />
				right-backward
			</label>
		</fieldset>
	</fieldset>
	<button on:click={() => dispatch('delete')}>Delete</button>
	<button on:click={() => dispatch('duplicate')}>Duplicate</button>
	<label>
		<input type="checkbox" bind:checked={command.skip} />
		skip
	</label>
</section>

<style>
	section {
		display: grid;
		flex-direction: row;
		align-items: center;
		grid-template-columns: repeat(3, 10em) min-content;
		grid-template-rows: repeat(2, 1fr);
		grid-auto-flow: column;
		padding: 0.5em;
		margin-top: -2px;
		border: 2px dashed black;
	}
	fieldset {
		display: contents;
	}
	input[type='number'] {
		width: 5em;
	}
</style>
