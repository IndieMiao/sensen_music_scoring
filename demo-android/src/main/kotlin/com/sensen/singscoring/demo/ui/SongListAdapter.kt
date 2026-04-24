package com.sensen.singscoring.demo.ui

import android.content.Context
import android.graphics.drawable.GradientDrawable
import android.util.TypedValue
import android.view.Gravity
import android.view.View
import android.view.ViewGroup
import android.widget.LinearLayout
import android.widget.TextView
import androidx.core.content.ContextCompat
import androidx.recyclerview.widget.RecyclerView
import com.sensen.singscoring.demo.SongCatalog

class SongListAdapter(
    private val onClick: (SongCatalog.Song) -> Unit,
    private val onFilterApplied: (visibleCount: Int) -> Unit,
) : RecyclerView.Adapter<SongListAdapter.RowViewHolder>() {

    private var allSongs: List<SongCatalog.Song> = emptyList()
    private var filtered: List<SongCatalog.Song> = emptyList()
    private var query: String = ""
    private var highlightedId: String? = null

    fun setSongs(songs: List<SongCatalog.Song>) {
        allSongs = songs
        applyFilter()
    }

    fun setQuery(q: String) {
        val normalized = q.trim()
        if (normalized == query) return
        query = normalized
        applyFilter()
    }

    fun setHighlightedId(id: String?) {
        if (id == highlightedId) return
        highlightedId = id
        notifyDataSetChanged()
    }

    private fun applyFilter() {
        filtered = if (query.isEmpty()) {
            allSongs
        } else {
            allSongs.filter { s ->
                s.name.contains(query, ignoreCase = true) ||
                    s.singer.contains(query, ignoreCase = true)
            }
        }
        notifyDataSetChanged()
        onFilterApplied(filtered.size)
    }

    override fun getItemCount(): Int = filtered.size

    override fun onCreateViewHolder(parent: ViewGroup, viewType: Int): RowViewHolder {
        return RowViewHolder.create(parent.context)
    }

    override fun onBindViewHolder(holder: RowViewHolder, position: Int) {
        val song = filtered[position]
        holder.bind(song, isHighlighted = song.id == highlightedId, onClick = onClick)
    }

    class RowViewHolder private constructor(
        private val container: LinearLayout,
        private val highlightBar: View,
        private val title: TextView,
        private val subtitle: TextView,
    ) : RecyclerView.ViewHolder(container) {

        fun bind(
            song: SongCatalog.Song,
            isHighlighted: Boolean,
            onClick: (SongCatalog.Song) -> Unit,
        ) {
            title.text = song.name
            val sub = buildString {
                append(song.singer)
                if (song.rhythm.isNotEmpty()) {
                    if (song.singer.isNotEmpty()) append("  •  ")
                    append(song.rhythm)
                }
            }
            subtitle.text = sub
            subtitle.visibility = if (sub.isEmpty()) View.GONE else View.VISIBLE
            highlightBar.visibility = if (isHighlighted) View.VISIBLE else View.INVISIBLE
            container.setOnClickListener { onClick(song) }
        }

        companion object {
            fun create(context: Context): RowViewHolder {
                val rowBg = GradientDrawable().apply {
                    setColor(Palette.SURFACE)
                    cornerRadius = 12.dp(context).toFloat()
                }

                val container = LinearLayout(context).apply {
                    orientation = LinearLayout.HORIZONTAL
                    gravity = Gravity.CENTER_VERTICAL
                    isClickable = true
                    isFocusable = true
                    background = rowBg
                    // Ripple overlay on top of the rounded dark fill:
                    val tv = TypedValue()
                    context.theme.resolveAttribute(
                        android.R.attr.selectableItemBackground, tv, true
                    )
                    foreground = ContextCompat.getDrawable(context, tv.resourceId)
                    setPadding(
                        20.dp(context), 12.dp(context),
                        20.dp(context), 12.dp(context),
                    )
                    layoutParams = RecyclerView.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                    ).apply {
                        topMargin = 6.dp(context)
                        bottomMargin = 6.dp(context)
                    }
                }

                val highlightBar = View(context).apply {
                    setBackgroundColor(Palette.ACCENT)
                    layoutParams = LinearLayout.LayoutParams(
                        3.dp(context),
                        36.dp(context),
                    ).apply { marginEnd = 12.dp(context) }
                    visibility = View.INVISIBLE
                }

                val textCol = LinearLayout(context).apply {
                    orientation = LinearLayout.VERTICAL
                    layoutParams = LinearLayout.LayoutParams(
                        0,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                        1f,
                    )
                }

                val title = TextView(context).apply {
                    textSize = 18f
                    setTextColor(Palette.TEXT_PRIMARY)
                }
                val subtitle = TextView(context).apply {
                    textSize = 13f
                    setTextColor(Palette.TEXT_SECONDARY)
                    layoutParams = LinearLayout.LayoutParams(
                        ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT,
                    ).apply { topMargin = 2.dp(context) }
                }

                textCol.addView(title)
                textCol.addView(subtitle)
                container.addView(highlightBar)
                container.addView(textCol)

                return RowViewHolder(container, highlightBar, title, subtitle)
            }
        }
    }
}
