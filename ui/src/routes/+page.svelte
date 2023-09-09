<script>
	import { variables } from '$lib/utils/variables';
	import { fade } from 'svelte/transition';
	import { Progress, Button } from 'sveltestrap';
	import { toast } from '@zerodevx/svelte-toast';
	import { onMount } from 'svelte';

	import Fa from 'svelte-fa/src/fa.svelte';
	import { faSync } from '@fortawesome/pro-solid-svg-icons/faSync';

	// ******* SHOW LEVEL ******** //
	let level = undefined;
	//level = [ { id: ... },... ]
	onMount(async () => {
		// initial level
		const response = await fetch(`/api/level/current/all`, {
			headers: { 'Content-type': 'application/json' }
		}).catch((error) => console.log(error));
		if (response.ok) {
			level = await response.json();
		} else {
			toast.push(`Error ${response.status} ${response.statusText}<br>Unable to request current level.`, variables.toast.error);
		}
	});

	let connected = false;
	let disconnected = false;
	onMount(async () => {
		// dynamic refreshing level
		if (window.EventSource) {
			var source = new EventSource('/api/events');

			source.addEventListener(
				'error',
				function (e) {
					if (e.target.readyState != EventSource.OPEN) {
						console.log('Events Disconnected');
						connected = false;
						disconnected = true;
					}
				},
				false
			);

			source.addEventListener(
				'message',
				function (e) {
					console.log('message', e.data);
				},
				false
			);

			source.addEventListener(
				'status',
				function (e) {
					try {
						level = JSON.parse(e.data);
						connected = true;
					} catch (error) {
						console.log(error);
						console.log('Error parsing status', e.data);
					}
				},
				false
			);
		}
	});

	// ******* REPRESSUREIZE ******** //
	let isVisible = [];
	async function repressurizeTube(sensor) {
		isVisible[sensor] = true;
		const response = await fetch(`/api/restore/pressure?sensor=${sensor + 1}`, {
			method: 'POST',
			headers: { 'Content-type': 'application/json' }
		}).catch((error) => console.log(error));
		if (!response.ok) {
			toast.push(`Error ${response.status} ${response.statusText}<br>Unable to repressurize tube of Sensor ${sensor + 1}.`, variables.toast.error);
		}
		isVisible[sensor] = false;
	}
</script>

<svelte:head>
	<title>Sensor Status</title>
</svelte:head>

<div class="row">
	{#if level == undefined}
		<div class="col">Requesting current level, please wait...</div>
	{:else if level.length == 0}
		<div class="col">No sensors installed</div>
	{:else}
		{#each { length: level.length } as _, i}
			{#if disconnected}
				<div class="col-sm-12">Disconnected! Last level of tank sensor {i + 1}, Atmospheric pressure: {level[i].airPressure.toFixed(2)} hPa</div>				
			{:else}
				<div class="col-sm-12">Current level of tank sensor {i + 1}, Atmospheric pressure: {level[i].airPressure.toFixed(2)} hPa</div>
			{/if}
			<div class="col-sm-9">
				{#if level[i].error }
					<Progress striped color="danger" value={100} style="height: 5rem;">Sensor Error!</Progress>
				{:else if !level[i].configured}
					<Progress striped color="danger" value={100} style="height: 5rem;">Sensor not yet calibrated. Current Reading: {level[i].sensorPressure}</Progress>
				{:else}
					<Progress animated={connected} striped value={level[i].level} style="height: 5rem;">{level[i].level}%<br />({Math.round(level[i].volume / 1000)} liter)</Progress>
				{/if}
			</div>
			<div class="col-sm-3">
				<Button on:click={() => repressurizeTube(i)} block style="height: 5rem;">
					{#if isVisible[i]}<div out:fade={{ delay: 5000 }}>
							<Fa icon={faSync} size="2x" spin />&nbsp;
						</div>{/if}
					Repressurize
				</Button>
			</div>
		{/each}
	{/if}
</div>
