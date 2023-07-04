<script lang="ts">
	import make_mqtt from 'https://cdn.jsdelivr.net/npm/u8-mqtt/esm/web/index.js';
	import CommandForm from './CommandForm.svelte';

	type Command = {
		lb?: boolean;
		lf?: boolean;
		rb?: boolean;
		rf?: boolean;
		/** Repeat count */
		r: number;
		skip?: boolean;
		is_stop_pkt: boolean;
	};

	let mqtt_client = make_mqtt({
		on_disconnect() {
			console.log('Disconnected');
		},
	}).with_websock('ws://192.168.0.110:8083');

	mqtt_client
		.connect({
			client_id: 'esp-app',
		})
		.then(async () => {
			mqtt_client
				.on_topic('esp/1/lego/cmd/callback', async (pkt) => {
					sendInProgress = false;
					lastSendStatus = pkt.text();
				})
				.on_topic('esp/1/status', async (pkt) => {
					const status = pkt.text();
					if (status === 'alive') isAlive = true;
					if (status === 'dead') isAlive = false;
				});
			await mqtt_client.subscribe(['esp/1/status', 'esp/1/lego/cmd/callback'], { qos: 1 });
		});

	let default_command = { is_stop_pkt: true, r: 1 };
	let isAlive = false;
	let commands: Command[] = [];
	let draggedIndex: number | undefined, droppedIndex: number | undefined;
	let sendInProgress = false;
	let lastSendStatus: string | undefined;

	function swapCommands() {
		if (draggedIndex === undefined || droppedIndex === undefined || draggedIndex === droppedIndex) {
			return;
		}
		const item = commands[draggedIndex];
		commands = commands.slice(0, draggedIndex).concat(commands.slice(draggedIndex + 1));
		commands = commands.slice(0, droppedIndex).concat(item).concat(commands.slice(droppedIndex));
	}
	function duplicateCommand(index: number) {
		commands = commands
			.slice(0, index)
			.concat({ ...commands[index] })
			.concat(commands.slice(index));
	}

	async function sendCommands() {
		sendInProgress = true;
		lastSendStatus = undefined;
		let lego_channel = 1;
		let raw = new Uint16Array(commands.reduce((acc, c) => acc + c.r, 0));
		let command_count = 0;
		for (let i = 0; i < commands.length; i++) {
			const command = commands[i];
			if (command.skip) {
				continue;
			}
			let short = lego_channel << 12;
			if (command.is_stop_pkt) {
				// Noop
			} else {
				let keys_pressed = 0;
				if (command.lb) {
					short |= 1 << 4;
					keys_pressed++;
				}
				if (command.lf) {
					short |= 1 << 5;
					keys_pressed++;
				}
				if (command.rf) {
					short |= 1 << 6;
					keys_pressed++;
				}
				if (command.rb) {
					short |= 1 << 7;
					keys_pressed++;
				}
				if (keys_pressed > 1) {
					short |= 1 << 15;
				}
			}
			for (let j = 0; j < command.r; j++) {
				raw.set([short], command_count);
				command_count++;
			}
		}
		const payload = new Uint8Array(raw.buffer);
		await mqtt_client.publish({ topic: `esp/1/lego/cmd/append`, payload });
	}

	let joystickButton = 0;
	const buttonBuffer = new Uint8Array(1);

	function setButton(v: number) {
		joystickButton |= v;
		buttonBuffer.set([joystickButton]);
		mqtt_client.publish({
			topic: 'esp/1/lego/button',
			payload: buttonBuffer,
			qos: 0,
		});
	}

	function resetButton(v: number) {
		joystickButton &= ~v;
		buttonBuffer.set([joystickButton]);
		mqtt_client.publish({
			topic: 'esp/1/lego/button',
			payload: buttonBuffer,
			qos: 0,
		});
	}
</script>

<ol>
	{#each commands as command, index}
		<li
			draggable="true"
			on:dragstart={() => (draggedIndex = index)}
			on:dragend={() => (droppedIndex = undefined)}
			on:dragover={(e) => {
				e.preventDefault();
				droppedIndex = index;
			}}
			on:drop={swapCommands}
			class:draggedover={index === droppedIndex && index !== draggedIndex}
		>
			<CommandForm
				bind:command
				on:delete={() => (commands = commands.slice(0, index).concat(commands.slice(index + 1)))}
				on:duplicate={() => duplicateCommand(index)}
			/>
		</li>
	{/each}
</ol>

<button on:click={() => (commands = [...commands, { ...default_command }])}> New command </button>
<button on:click={sendCommands} disabled={sendInProgress || !isAlive}>Send</button>
<p>
	Connection status:
	<b>{isAlive ? 'connected' : 'not connected'}</b>
</p>
<p>
	Last send status:
	<b>{lastSendStatus ?? '-'}</b>
</p>

<section class="joystick">
	<button
		on:pointerdown={() => setButton(0x2)}
		on:pointerup={() => resetButton(0x2)}
		on:pointercancel={() => resetButton(0x2)}>^</button
	>
	<button
		on:pointerdown={() => setButton(0x1)}
		on:pointerup={() => resetButton(0x1)}
		on:pointercancel={() => resetButton(0x1)}>v</button
	>
	<code>0x{joystickButton.toString(16)}</code>
	<span />
	<button
		on:pointerdown={() => setButton(0x4)}
		on:pointerup={() => resetButton(0x4)}
		on:pointercancel={() => resetButton(0x4)}>^</button
	>
	<button
		on:pointerdown={() => setButton(0x8)}
		on:pointerup={() => resetButton(0x8)}
		on:pointercancel={() => resetButton(0x8)}>v</button
	>
</section>

<style>
	ol {
		user-select: none;
	}
	.draggedover {
		background-color: rgba(0 0 0 / 0.1);
	}
	.joystick {
		touch-action: none;
		display: grid;
		gap: 1em;
		grid-template-columns: 1fr 1fr 1fr;
		grid-template-rows: 4em 4em;
		grid-auto-flow: column;
	}
</style>
