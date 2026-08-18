import type { ReactNode } from 'react'

interface PanelProps {
  title: string
  note?: ReactNode
  area: string
  children: ReactNode
}

export function Panel({ title, note, area, children }: PanelProps) {
  return (
    <section className="panel" style={{ gridArea: area }}>
      <header className="panel__head">
        <h2 className="panel__title">{title}</h2>
        {note !== undefined && <span className="panel__note numeric">{note}</span>}
      </header>
      <div className="panel__body">{children}</div>
    </section>
  )
}

/** Reserved space for a panel not yet built, so the layout can be reviewed whole. */
export function PlaceholderPanel({ title, area }: { title: string; area: string }) {
  return (
    <section className="panel panel--placeholder" style={{ gridArea: area }}>
      <h2 className="panel__title">{title}</h2>
      <span style={{ fontSize: 'var(--size-label)' }}>next step</span>
    </section>
  )
}
