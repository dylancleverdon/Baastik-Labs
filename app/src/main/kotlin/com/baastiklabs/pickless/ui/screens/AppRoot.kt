package com.baastiklabs.pickless.ui.screens

import android.net.Uri
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyListScope
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material.icons.filled.CalendarMonth
import androidx.compose.material.icons.filled.Home
import androidx.compose.material.icons.filled.Insights
import androidx.compose.material.icons.filled.Menu
import androidx.compose.material.icons.filled.QrCodeScanner
import androidx.compose.material.icons.outlined.Lock
import androidx.compose.material3.Button
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.staticCompositionLocalOf
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import androidx.navigation.NavHostController
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.baastiklabs.pickless.Container
import com.baastiklabs.pickless.data.Prefs
import kotlinx.coroutines.flow.MutableStateFlow

val LocalContainer = staticCompositionLocalOf<Container> { error("no container") }
val LocalNav = staticCompositionLocalOf<NavHostController> { error("no nav") }
val LocalSnack = staticCompositionLocalOf<SnackbarHostState> { error("no snackbar") }

/** Settings as already loaded by [AppRoot], so screens can lay out fully on their first frame. */
val LocalPrefs = compositionLocalOf<Prefs> { error("no prefs") }

/** A coroutine scope for click handlers that always resumes on the main thread (navigation needs it). */
@Composable
fun rememberUiScope(): kotlinx.coroutines.CoroutineScope = androidx.compose.runtime.rememberCoroutineScope { kotlinx.coroutines.Dispatchers.Main.immediate }

fun enc(s: String): String = Uri.encode(s)
fun dec(s: String?): String = Uri.decode(s ?: "")

private data class Tab(val route: String, val label: String, val icon: androidx.compose.ui.graphics.vector.ImageVector)

private val tabs = listOf(
    Tab("today", "Today", Icons.Filled.Home),
    Tab("check", "Check", Icons.Filled.QrCodeScanner),
    Tab("history", "History", Icons.Filled.CalendarMonth),
    Tab("insights", "Insights", Icons.Filled.Insights),
    Tab("more", "More", Icons.Filled.Menu),
)

@Composable
fun AppRoot(container: Container, routes: MutableStateFlow<String?>, locked: Boolean, onUnlock: () -> Unit) {
    val nav = rememberNavController()
    val snack = remember { SnackbarHostState() }
    val prefs by container.settings.prefs.collectAsState(initial = null)
    val request by routes.collectAsState()
    LaunchedEffect(request, prefs?.onboarded) {
        val r = request ?: return@LaunchedEffect
        if (prefs?.onboarded == true) { nav.navigate(r) { launchSingleTop = true }; routes.value = null }
    }
    if (locked) { LockScreen(onUnlock); return }
    val p = prefs ?: return
    CompositionLocalProvider(LocalContainer provides container, LocalNav provides nav, LocalSnack provides snack, LocalPrefs provides p) {
        val entry by nav.currentBackStackEntryAsState()
        val current = entry?.destination?.route
        Scaffold(
            snackbarHost = { SnackbarHost(snack) },
            bottomBar = {
                if (current in tabs.map { it.route }) {
                    NavigationBar {
                        tabs.forEach { t ->
                            NavigationBarItem(
                                selected = current == t.route,
                                onClick = { nav.navigate(t.route) { popUpTo("today") { saveState = true }; launchSingleTop = true; restoreState = true } },
                                icon = { Icon(t.icon, null) }, label = { Text(t.label) },
                            )
                        }
                    }
                }
            },
        ) { pad ->
            Box(Modifier.padding(bottom = pad.calculateBottomPadding())) {
                NavHost(nav, startDestination = if (p.onboarded) "today" else "onboarding") {
                    composable("onboarding") { OnboardingScreen() }
                    composable("today") { TodayScreen() }
                    composable("check") { CheckScreen() }
                    composable("history") { HistoryScreen() }
                    composable("insights") { InsightsScreen() }
                    composable("more") { MoreScreen() }
                    composable("scan?mode={mode}") { ScanScreen(it.arguments?.getString("mode") ?: "product") }
                    composable("label?code={code}&name={name}") { LabelScreen(dec(it.arguments?.getString("code")).ifBlank { null }, dec(it.arguments?.getString("name")).ifBlank { null }) }
                    composable("result/{ref}") { ResultScreen(dec(it.arguments?.getString("ref"))) }
                    composable("log/{ref}") { LogFoodScreen(dec(it.arguments?.getString("ref"))) }
                    composable("bm") { BmScreen() }
                    composable("bm/{id}") { BmScreen(it.arguments?.getString("id")?.toLongOrNull()) }
                    composable("symptom") { SymptomScreen() }
                    composable("checkin") { CheckInScreen(null) }
                    composable("checkin/{day}") { CheckInScreen(it.arguments?.getString("day")?.toLongOrNull()) }
                    composable("weekly") { WeeklyScreen() }
                    composable("day/{day}") { DayScreen(it.arguments?.getString("day")?.toLongOrNull() ?: 0) }
                    composable("explain/{id}") { ExplainScreen(it.arguments?.getString("id")?.toLongOrNull() ?: 0) }
                    composable("suspect/{key}") { SuspectScreen(dec(it.arguments?.getString("key"))) }
                    composable("avoid") { AvoidListScreen() }
                    composable("coach") { CoachScreen() }
                    composable("report") { ReportScreen() }
                    composable("card") { RestaurantCardScreen() }
                    composable("foods") { MyFoodsScreen() }
                    composable("recipe/{id}") { RecipeScreen(it.arguments?.getString("id")?.toLongOrNull()) }
                    composable("suggest") { SuggestionScreen(null) }
                    composable("suggest/{prefill}") { SuggestionScreen(dec(it.arguments?.getString("prefill"))) }
                    composable("settings") { SettingsScreen() }
                    composable("privacy") { PrivacyScreen() }
                }
            }
        }
    }
}

@Composable
private fun LockScreen(onUnlock: () -> Unit) {
    Column(Modifier.fillMaxSize().padding(32.dp), verticalArrangement = Arrangement.Center, horizontalAlignment = Alignment.CenterHorizontally) {
        Icon(Icons.Outlined.Lock, null, Modifier.padding(8.dp))
        Text("Pickless is locked", style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(16.dp))
        Button(onClick = onUnlock) { Text("Unlock") }
    }
}

/** Standard screen: top bar with back arrow and a scrolling column. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun Page(
    title: String,
    back: Boolean = true,
    actions: @Composable () -> Unit = {},
    bottom: (@Composable () -> Unit)? = null,
    content: LazyListScope.() -> Unit,
) {
    val nav = LocalNav.current
    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(title, style = MaterialTheme.typography.titleLarge) },
                navigationIcon = { if (back) IconButton(onClick = { nav.popBackStack() }) { Icon(Icons.AutoMirrored.Filled.ArrowBack, "Back") } },
                actions = { actions() },
                colors = TopAppBarDefaults.topAppBarColors(containerColor = MaterialTheme.colorScheme.background),
            )
        },
        bottomBar = { bottom?.invoke() },
        containerColor = MaterialTheme.colorScheme.background,
    ) { pad ->
        LazyColumn(Modifier.fillMaxSize().padding(pad), contentPadding = PaddingValues(start = 16.dp, end = 16.dp, bottom = 32.dp)) { content() }
    }
}
